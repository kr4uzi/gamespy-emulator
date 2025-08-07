#pragma once
#include "asio.h"
#include <cstdint>
#include <string>
#include <span>

namespace gamespy {
	class GameDB;
	class PlayerDB;
	struct TextPacket;

	class StatsClient {
		boost::asio::ip::tcp::socket m_Socket;
		std::string m_RecvBuffer;
		std::string::size_type m_LastPacketSize = 0;

		GameDB& m_GameDB;
		PlayerDB& m_PlayerDB;
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