#include "stats.client.h"
#include "utils.h"
#include "playerdb.h"
#include "gamedb.h"
#include "textpacket.h"
#include <print>
#include <ctime>
#include <sstream>
using namespace gamespy;

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
}

StatsClient::StatsClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB)
	: m_Socket(std::move(socket)), m_GameDB(gameDB), m_PlayerDB(playerDB), m_SessionKey(0)
{

}

StatsClient::~StatsClient()
{

}

boost::asio::awaitable<std::optional<TextPacket>> StatsClient::ReceivePacket()
{
	auto [error, length] = co_await boost::asio::async_read_until(m_Socket, m_RecvBuffer, TextPacket::PACKET_END, boost::asio::as_tuple(boost::asio::use_awaitable));
	if (error) {
		co_return std::nullopt;
	}

	auto packet = std::string{ boost::asio::buffer_cast<const char*>(m_RecvBuffer.data()), m_RecvBuffer.size() };

	// the client encodes all bytes except the trailing /final/
	auto encoded = std::span{ packet.begin(), packet.end() - TextPacket::PACKET_END.length() };
	utils::gs_xor(encoded, utils::xor_types::GameSpy3D);

	m_RecvBuffer.consume(packet.size());

	// the parse can only fail with invalidty, not incompleteness because /final/ is guaranteed to be present
	// in case of invalidity the caller of ReceivePacket needs to decide wether to abort or not
	auto textPacket = TextPacket::parse(packet);
	if (textPacket)
		co_return *textPacket;

	co_return std::nullopt;
}

boost::asio::awaitable<void> StatsClient::SendPacket(std::string message)
{
	utils::gs_xor(message, utils::xor_types::GameSpy3D);
	co_await m_Socket.async_send(boost::asio::buffer(std::format("{}{}", message, TextPacket::PACKET_END)), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> StatsClient::Process()
{
	if (!co_await Authenticate())
		co_return;

	auto packet = std::optional<TextPacket>{};
	while (packet = co_await ReceivePacket()) {
		if (packet->type == "getpid") {
			// "\getpid\\nick\%s\keyhash\%s\lid\%d"
			std::println("[stats] {}", packet->str());
		}
		else if (packet->type == "getpd") {
			// "\getpd\\pid\%d\ptype\%d\dindex\%d\keys\%s\lid\%d"
			std::println("[stats] {}", packet->str());
		}
		else if (packet->type == "authp") {
			std::println("[stats] {}", packet->str());

			auto& values = packet->values;
			const auto& localID = values["lid"];
			if (localID.empty()) {
				co_await SendPacket(R"(\error\\err\0\fatal\\errmsg\missing lid parameter\id\1)");
				continue;
			}

			if (values.contains("authtoken")) {
				const auto& authToken = values["authtoken"];
				const auto& challenge = values["resp"];
				// TODO: investigate how an authToken is tied to a specific challenge
				// https://github.com/ntrtwl/NitroDWC/blob/main/include/gs/dummy_auth.h
				// PreAuthenticatePlayerPartner: \authp\\authtoken\%s\resp\%s\lid\%d
			}
			else if (values.contains("pid")) {
				// PreAuthenticatePlayerPM: \authp\\pid\%d\resp\%s\lid\%d
				auto pid = std::stoul(packet->values["pid"]);
				const auto& player = co_await m_PlayerDB.GetPlayerByPID(pid);
				if (player) {
					if (values["resp"] == utils::md5(std::format("{}{}", player->password, ::create_challenge(m_SessionKey ^ ::CHALLENGEXOR))))
						co_await SendPacket(std::format(R"(\pauthr\{}\lid\{})", player->GetProfileID(), localID));
					else
						co_await SendPacket(std::format(R"(\pauthr\-1\lid\{})", localID));
				}
				else {
					co_await SendPacket(std::format(R"(\pauthr\-1\lid\{})", localID));
				}
			}
			else if (values.contains("nick")) {
				// PreAuthenticatePlayerCD: \authp\\nick\%s\keyhash\%s\resp\%s\lid\%d
				// during profile creation, the cd key can be linked to a players profile
				// not sure what the resp is here though...

			}						
		}
		else if (packet->type == "setpd") {
			// "\setpd\\pid\%d\ptype\%d\dindex\%d\kv\%d\lid\%d\length\%d\data\"
			std::println("[stats] {}", packet->str());
		}
		if (packet->type == "updgame") {
			auto& gamedata = packet->values["gamedata"];
			std::replace(gamedata.begin(), gamedata.end(), '\x1', '\\');
			std::println("[stats] {}", packet->str());
			// "\updgame\\sesskey\%d\done\%d\gamedata\%s"
			// "\updgame\\sesskey\%d\connid\%d\done\%d\gamedata\%s"
			// "\updgame\\sesskey\%d\connid\%d\done\%d\gamedata\%s\dl\1"
		}
		else if (packet->type == "newgame") {
			// "\newgame\\connid\%d\sesskey\%d"
			// "\newgame\\sesskey\%d\challenge\%d"
			std::println("[stats] {}", packet->str());
		}
	}
}

boost::asio::awaitable<bool> StatsClient::Authenticate()
{
	// the client expects 38 bytes minimum => min challenge length is 10
	m_ServerChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10);

	co_await SendPacket(std::format(R"(\lc\1\challenge\{}\id\1)", m_ServerChallenge));

	auto packet = co_await ReceivePacket();
	if (!packet || packet->type != "auth") {
		std::println("[stats] received invalid response during authentication");
		co_return false;
	}

	// "\auth\\gamename\%s\response\%s\port\%d\id\1"
	const auto gamename = std::string{ packet->values["gamename"] };
	if (!m_GameDB.HasGame(gamename)) {
		std::println("[stats] unknown game {}", gamename);
		co_await SendPacket(R"(\error\\err\0\fatal\\errmsg\Unknown Game!\id\1)");
		co_return false;
	}

	const auto& game = m_GameDB.GetGame(gamename);
	const auto challenge = std::format("{}{}", g_crc32(m_ServerChallenge), game.Data().secretKey);
	const auto challengeHash = utils::md5(challenge);
	if (packet->values["response"] != challengeHash) {
		std::println("[stats] received invalid response");
		co_await SendPacket(R"(\error\\err\0\fatal\\errmsg\Invalid Response!\id\1)");
		co_return false; 
	}

	m_SessionKey = static_cast<decltype(m_SessionKey)>(std::time(nullptr));
	if (m_SessionKey < 0) m_SessionKey = -m_SessionKey;

	co_await SendPacket(std::format(R"(\lc\2\sesskey\{}\proof\0\id\1)", m_SessionKey));
	co_return true;
}