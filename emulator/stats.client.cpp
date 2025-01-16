#include "stats.client.h"
#include "utils.h"
#include "textpacket.h"
#include <print>
#include <ctime>
using namespace gamespy;

StatsClient::StatsClient(boost::asio::ip::tcp::socket socket, GameDB& db)
	: m_Socket(std::move(socket)), m_DB(db)
{

}

StatsClient::~StatsClient()
{

}

// GameSpy: gstats/gstats.c
std::int32_t g_crc32(const std::string& str)
{
	static constexpr std::int32_t MULTIPLIER = -1664117991;
	std::int32_t hash = 0;

	for (const auto& c : str)
		hash = hash * MULTIPLIER + c;

	return hash;
}

boost::asio::awaitable<void> StatsClient::Process()
{
	co_await SendChallenge();

	auto endpoint = m_Socket.local_endpoint();
	auto buffer = boost::asio::streambuf{};

	while (m_Socket.is_open()) {
		// the client encodes all bytes except the trailing /final/
		auto [error, length] = co_await boost::asio::async_read_until(m_Socket, buffer, TextPacket::PACKET_END, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error) {
			break;
		}

		auto bytes = std::string{ boost::asio::buffer_cast<const char*>(buffer.data()), buffer.size() };
		auto encoded = std::span{ bytes.begin(), bytes.end() - TextPacket::PACKET_END.length() };
		utils::gs_xor(encoded, utils::xor_types::gamespy3d);

		auto packet = TextPacket::parse(bytes);
		if (!packet) {
			std::println("[stats] failed to parse packet");
			break;
		}

		buffer.consume(buffer.size());
		// > \auth\\gamename\%s\response\%s\port\%d\id\1 (\final\)
		// < \lc\2\sesskey\{}\proof\0\id\1\final\
		// > stats.dat

		if (packet->type == "auth") {
			const auto gamename = std::string{ packet->values["gamename"] };
			if (!m_DB.HasGame(gamename)) {
				std::println("[stats] unknown game {}", gamename);

				auto response = std::format(R"(\error\\err\0\fatal\\errmsg\Unknown Game!\id\1\final\)");
				utils::gs_xor(response, utils::xor_types::gamespy3d);

				co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
				break;
			}

			const auto& game = m_DB.GetGame(gamename);


			const auto challenge = std::format("{}{}", g_crc32(m_ServerChallenge), game.Data().secretKey);
			const auto challengeHash = utils::md5(challenge);
			if (packet->values["response"] == challengeHash) {
				m_SessionKey = static_cast<decltype(m_SessionKey)>(std::time(nullptr));
				if (m_SessionKey < 0) m_SessionKey = -m_SessionKey;

				auto response = std::format(R"(\lc\2\sesskey\{}\proof\0\id\1\final\)", m_SessionKey);
				utils::gs_xor(response, utils::xor_types::gamespy3d);

				co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
			}
			else {
				std::println("[stats] received invalid response");

				auto response = std::format(R"(\error\\err\0\fatal\\errmsg\Invalid Response!\id\1\final\)");
				utils::gs_xor(response, utils::xor_types::gamespy3d);

				co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
				break;
			}
		}
		else {
			std::println("[stats] received: {}", packet->type);
		}
	}
}

boost::asio::awaitable<void> StatsClient::SendChallenge()
{
	// the client expects 38 bytes minimum => min challenge length is 10
	m_ServerChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10);

	auto response = std::format(R"(\lc\1\challenge\{}\id\1\final\)", m_ServerChallenge);
	utils::gs_xor(response, utils::xor_types::gamespy3d);
	co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);

	m_State = STATES::AUTHENTICATING;
}