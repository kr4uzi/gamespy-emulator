#pragma once
#include "asio.h"
#include <string>
#include <variant>
#include <map>

namespace gamespy {
	class GameDB;
	class PlayerDB;
	struct TextPacket;

	class StatsClient {
		boost::asio::ip::tcp::socket m_Socket;
		boost::asio::streambuf m_RecvBuffer;

		PlayerDB& m_PlayerDB;
		GameDB& m_GameDB;
		std::string m_ServerChallenge;
		std::int32_t m_SessionKey;

	public:
		StatsClient() = delete;
		StatsClient(const StatsClient& rhs) = delete;
		StatsClient& operator=(const StatsClient& rhs) = delete;

		StatsClient(StatsClient&& rhs) = default;
		StatsClient& operator=(StatsClient&& rhs) = default;

		StatsClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB);
		~StatsClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<std::optional<TextPacket>> ReceivePacket();
		boost::asio::awaitable<void> SendPacket(std::string message);

		boost::asio::awaitable<bool> Authenticate();
	};
}