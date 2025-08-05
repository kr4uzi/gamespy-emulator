#pragma once
#include "asio.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <span>

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
		StatsClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB);
		~StatsClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<std::span<char>> ReceivePacket();
		boost::asio::awaitable<void> SendPacket(std::string message);

		boost::asio::awaitable<bool> Authenticate();
	};
}