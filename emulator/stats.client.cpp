#include "stats.client.h"
#include "utils.h"
#include "playerdb.h"
#include "gamedb.h"
#include "game.h"
#include <print>
#include <ctime>
#include <sstream>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
using namespace gamespy;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace {
	// functions taken from the GameSpy SDK (gstats/gstats.c) and modified to use modern c++

	// when authenticating the player using the sessionkey+password, a special encoding is used (which is not used anywhere else)
	static constexpr auto CHALLENGEXOR = 0x38F371E6;

	std::string create_challenge(std::int32_t challenge)
	{
		auto hex = std::format("{:08x}", challenge);
		char i = 0;
		for (auto& c : hex)
			c += ('A' - '0') + (i++);

		return hex;
	}

	std::int32_t g_crc32(const std::string& str)
	{
		static constexpr std::int32_t MULTIPLIER = -1664117991;
		std::int32_t hash = 0;

		for (const auto& c : str)
			hash = hash * MULTIPLIER + c;

		return hash;
	}

	boost::asio::experimental::coro<std::span<char>> packet_reader(boost::asio::ip::tcp::socket& sock)
	{
		constexpr auto packetEnd = std::string_view{ "\\final\\" };
		std::string buffer;
		while (sock.is_open()) {
			const auto& [error, length] = co_await boost::asio::async_read_until(sock, boost::asio::dynamic_buffer(buffer), packetEnd, boost::asio::as_tuple);
			if (error) break;

			auto encoded = std::span(buffer.data(), length - packetEnd.size());
			utils::gs_xor(encoded, utils::xor_types::GameSpy3D);
			co_yield std::span(buffer.data(), length);

			buffer.erase(0, length);
		}
	}
}

StatsClient::StatsClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB, const std::optional<boost::asio::ip::tcp::endpoint>& snapshotEndpoint)
	: m_Socket(std::move(socket)), m_GameDB(gameDB), m_PlayerDB(playerDB), m_SessionKey(0), m_SnapshotEndpoint(snapshotEndpoint)
{

}

StatsClient::~StatsClient()
{

}

boost::asio::awaitable<void> StatsClient::SendPacket(std::string message)
{
	utils::gs_xor(message, utils::xor_types::GameSpy3D);
	co_await m_Socket.async_send(boost::asio::buffer(std::format("{}\\final\\", message)), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> StatsClient::Process()
{
	auto reader = ::packet_reader(m_Socket);
	if (!co_await Authenticate(reader))
		co_return;

	while (true) {
	//for (auto _packet = co_await ReceivePacket(); !_packet.empty(); _packet = co_await ReceivePacket()) {
		auto __packet = co_await reader.async_resume(boost::asio::use_awaitable);
		if (!__packet) break;
		auto& _packet = *__packet;
		auto packet = std::string_view{ _packet };
		if (packet.starts_with("\\getpid\\")) {
			// "\getpid\\nick\%s\keyhash\%s\lid\%d"
			std::println("[stats] {}", packet);
		}
		else if (packet.starts_with("\\getpd\\")) {
			// "\getpd\\pid\%d\ptype\%d\dindex\%d\keys\%s\lid\%d"
			std::println("[stats] {}", packet);
		}
		else if (packet.starts_with("\\authp\\")) {
			std::println("[stats] {}", packet);
			auto localID = utils::value_for_key(_packet, "\\lid\\");
			if (!localID || localID->empty()) {
				co_await SendPacket(R"(\error\\err\0\fatal\\errmsg\missing lid parameter\id\1)");
				continue;
			}

			auto authToken = utils::value_for_key(_packet, "\\authtoken\\");
			auto resp = utils::value_for_key(_packet, "\\resp\\");
			if (authToken) {
				// TODO: investigate how an authToken is tied to a specific challenge
				// https://github.com/ntrtwl/NitroDWC/blob/main/include/gs/dummy_auth.h
				// PreAuthenticatePlayerPartner: \authp\\authtoken\%s\resp\%s\lid\%d
			}
			else if (auto pid = utils::value_for_key<std::uint32_t>(_packet, "\\pid\\"); pid) {
				// PreAuthenticatePlayerPM: \authp\\pid\%d\resp\%s\lid\%d
				const auto& player = co_await m_PlayerDB.GetPlayerByPID(*pid);
				if (player && resp) {
					if (*resp == utils::md5(std::format("{}{}", player->password, ::create_challenge(m_SessionKey ^ ::CHALLENGEXOR))))
						co_await SendPacket(std::format(R"(\pauthr\{}\lid\{})", player->GetProfileID(), *localID));
					else
						co_await SendPacket(std::format(R"(\pauthr\-1\lid\{})", *localID));
				}
				else {
					co_await SendPacket(std::format(R"(\pauthr\-1\lid\{})", *localID));
				}
			}
			else if (auto nick = utils::value_for_key(_packet, "\\nick\\"); nick) {
				// PreAuthenticatePlayerCD: \authp\\nick\%s\keyhash\%s\resp\%s\lid\%d
				// during profile creation, the cd key can be linked to a players profile
				// not sure what the resp is here though...

			}						
		}
		else if (packet.starts_with("\\setpd\\")) {
			// "\setpd\\pid\%d\ptype\%d\dindex\%d\kv\%d\lid\%d\length\%d\data\"
			std::println("[stats] {}", packet);
		}
		else if (packet.starts_with("\\updgame\\")) {
			auto gamedata = utils::value_for_key<std::string>(_packet, "\\gamedata\\");
			if (gamedata) {
				std::replace(gamedata->begin(), gamedata->end(), '\x1', '\\');
				auto done = utils::value_for_key<std::uint32_t>(_packet, "\\done\\");
				co_await HandeSnapshot(*gamedata, done && *done == 1);
			}

			// "\updgame\\sesskey\%d\done\%d\gamedata\%s"
			// "\updgame\\sesskey\%d\connid\%d\done\%d\gamedata\%s"
			// "\updgame\\sesskey\%d\connid\%d\done\%d\gamedata\%s\dl\1"
		}
		else if (packet.starts_with("\\newgame\\")) {
			// "\newgame\\connid\%d\sesskey\%d"
			// "\newgame\\sesskey\%d\challenge\%d"
			//std::println("[stats] {}", packet);
		}
		else {
			std::println("[stats] {}", packet);
		}
	}
}

boost::asio::awaitable<bool> StatsClient::Authenticate(boost::asio::experimental::coro<std::span<char>>& reader)
{
	// the client expects 38 bytes minimum => min challenge length is 10
	m_ServerChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10);

	co_await SendPacket(std::format(R"(\lc\1\challenge\{}\id\1)", m_ServerChallenge));

	//auto _packet = co_await ReceivePacket();
	auto __packet = co_await reader.async_resume(boost::asio::use_awaitable);
	if (!__packet) co_return false;
	auto& _packet = *__packet;
	auto packet = std::string_view{ _packet };
	auto gamename = utils::value_for_key(_packet, "\\gamename\\");
	if (packet.empty() || !packet.starts_with("\\auth\\") || !gamename || gamename->empty()) {
		std::println("[stats] received invalid response during authentication\n{}", packet);
		co_return false;
	}

	// "\auth\\gamename\%s\response\%s\port\%d\id\1"
	if (!co_await m_GameDB.HasGame(*gamename)) {
		std::println("[stats] unknown game {}", *gamename);
		co_await SendPacket(R"(\error\\err\0\fatal\\errmsg\Unknown Game!\id\1)");
		co_return false;
	}

	const auto& game = co_await m_GameDB.GetGame(*gamename);
	const auto challenge = std::format("{}{}", g_crc32(m_ServerChallenge), game->secretKey());
	const auto challengeHash = utils::md5(challenge);
	if (auto response = utils::value_for_key(_packet, "\\response\\"); !response || *response != challengeHash) {
		std::println("[stats] received invalid response");
		co_await SendPacket(R"(\error\\err\0\fatal\\errmsg\Invalid Response!\id\1)");
		co_return false; 
	}

	m_SessionKey = static_cast<decltype(m_SessionKey)>(std::time(nullptr));
	if (m_SessionKey < 0) m_SessionKey = -m_SessionKey;

	co_await SendPacket(std::format(R"(\lc\2\sesskey\{}\proof\0\id\1)", m_SessionKey));
	co_return true;
}

boost::asio::awaitable<void> StatsClient::HandeSnapshot(const std::string_view& data, bool final)
{
	if (!m_SnapshotEndpoint) {
		std::println("[stats] snapshot endpoint not configured, skipping snapshot upload");
		co_return;
	}

	beast::tcp_stream stream(m_Socket.get_executor());
	co_await stream.async_connect(*m_SnapshotEndpoint, net::use_awaitable);

	auto req = http::request<http::string_body>{ http::verb::post, "/ASP/bf2statistics.php", 10 };
	req.set(http::field::user_agent, "GameSpyHTTP/1.0");
	// note: originaly, bf2 used "application/x-www-form-urlencoded",
	// but the data is actually json, so we use the correct content-type here
	req.set(http::field::content_type, "application/json");
	req.set(http::field::connection, "close");
	req.body() = data;
	req.prepare_payload();
	co_await http::async_write(stream, req, net::use_awaitable);

	beast::flat_buffer buffer;
	http::response<http::string_body> res;
	co_await http::async_read(stream, buffer, res, net::use_awaitable);

	std::println("[stats] snapshot processed");
}