#include "stats.client.h"
#include "utils.h"
#include "playerdb.h"
#include "gamedb.h"
#include "game.h"
#include <print>
#include <ctime>
#include <sstream>
#include <bit>
#include <chrono>
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
		static constexpr std::uint32_t MULTIPLIER = 0x9CCF9319;
		std::uint32_t hash = 0;

		for (const auto& c : str)
			hash = hash * MULTIPLIER + static_cast<unsigned char>(c);

		// GameSpy interprets the wrapping 32-bit result as a signed decimal value.
		return std::bit_cast<std::int32_t>(hash);
	}

	// cap the read buffer so a client that never sends the final terminator cannot grow
	// it without bound; snapshots can be large (end-of-round data), so allow headroom
	constexpr std::size_t max_snapshot_buffer_size = 512 * 1024;

}

StatsClient::StatsClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB, const std::optional<boost::asio::ip::tcp::endpoint>& snapshotEndpoint)
	: m_Socket(std::move(socket)), m_GameDB(gameDB), m_PlayerDB(playerDB), m_SnapshotEndpoint(snapshotEndpoint), m_SessionKey(0)
{

}

StatsClient::~StatsClient()
{

}

boost::asio::awaitable<void> StatsClient::SendPacket(std::string message)
{
	utils::gs_xor(message, utils::xor_types::GameSpy3D);
	message += "\\final\\";
	co_await boost::asio::async_write(m_Socket, boost::asio::buffer(message), boost::asio::use_awaitable);
}

boost::asio::awaitable<std::optional<std::string>> StatsClient::ReadPacket()
{
	constexpr auto packetEnd = std::string_view{ "\\final\\" };
	auto [error, length] = co_await boost::asio::async_read_until(m_Socket,
		boost::asio::dynamic_buffer(m_ReadBuffer, max_snapshot_buffer_size), packetEnd,
		boost::asio::as_tuple(boost::asio::use_awaitable));
	if (error) co_return std::nullopt;

	auto packet = m_ReadBuffer.substr(0, length);
	m_ReadBuffer.erase(0, length);
	auto encoded = std::span{ packet.data(), length - packetEnd.size() };
	utils::gs_xor(encoded, utils::xor_types::GameSpy3D);
	co_return packet;
}

boost::asio::awaitable<void> StatsClient::Process()
{
	if (!co_await Authenticate())
		co_return;

	while (true) {
		auto received = co_await ReadPacket();
		if (!received) break;
		auto& contents = *received;
		auto packet = std::string_view{ contents };
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
			auto localID = utils::value_for_key(contents, "\\lid\\");
			if (!localID || localID->empty()) {
				co_await SendPacket(R"(\error\\err\0\fatal\\errmsg\missing lid parameter\id\1)");
				continue;
			}

			auto authToken = utils::value_for_key(contents, "\\authtoken\\");
			auto resp = utils::value_for_key(contents, "\\resp\\");
			if (authToken) {
				// TODO: investigate how an authToken is tied to a specific challenge
				// https://github.com/ntrtwl/NitroDWC/blob/main/include/gs/dummy_auth.h
				// PreAuthenticatePlayerPartner: \authp\\authtoken\%s\resp\%s\lid\%d
			}
			else if (auto pid = utils::value_for_key<std::uint32_t>(contents, "\\pid\\"); pid) {
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
			else if (auto nick = utils::value_for_key(contents, "\\nick\\"); nick) {
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
			auto gamedata = utils::value_for_key<std::string>(contents, "\\gamedata\\");
			if (gamedata) {
				std::replace(gamedata->begin(), gamedata->end(), '\x1', '\\');
				co_await HandleSnapshot(*gamedata);
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

boost::asio::awaitable<bool> StatsClient::Authenticate()
{
	// the client expects 38 bytes minimum => min challenge length is 10
	m_ServerChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10);

	co_await SendPacket(std::format(R"(\lc\1\challenge\{}\id\1)", m_ServerChallenge));

	auto received = co_await ReadPacket();
	if (!received) co_return false;
	auto& contents = *received;
	auto packet = std::string_view{ contents };
	auto gamename = utils::value_for_key(contents, "\\gamename\\");
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
	if (auto response = utils::value_for_key(contents, "\\response\\"); !response || *response != challengeHash) {
		std::println("[stats] received invalid response");
		co_await SendPacket(R"(\error\\err\0\fatal\\errmsg\Invalid Response!\id\1)");
		co_return false; 
	}

	m_SessionKey = static_cast<std::uint32_t>(std::time(nullptr)) & 0x7FFFFFFFU;

	co_await SendPacket(std::format(R"(\lc\2\sesskey\{}\proof\0\id\1)", m_SessionKey));
	co_return true;
}

boost::asio::awaitable<void> StatsClient::HandleSnapshot(const std::string_view& data)
{
	if (!m_SnapshotEndpoint) {
		std::println("[stats] snapshot endpoint not configured, skipping snapshot upload");
		co_return;
	}

	beast::tcp_stream stream(m_Socket.get_executor());
	stream.expires_after(std::chrono::seconds(30));
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
	http::response_parser<http::string_body> response;
	response.body_limit(1024 * 1024);
	co_await http::async_read(stream, buffer, response, net::use_awaitable);
	if (response.get().result_int() < 200 || response.get().result_int() >= 300)
		throw std::runtime_error("snapshot endpoint returned an HTTP error");

	std::println("[stats] snapshot processed");
}
