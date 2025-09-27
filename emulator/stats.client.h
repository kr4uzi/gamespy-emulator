#pragma once
#include "asio.h"
#include <cstdint>
#include <string>
#include <span>
#include <boost/asio/experimental/coro.hpp>

namespace gamespy {
	class GameDB;
	class PlayerDB;
	struct TextPacket;

	class StatsClient {
		boost::asio::ip::tcp::socket m_Socket;

		GameDB& m_GameDB;
		PlayerDB& m_PlayerDB;
		std::string m_ServerChallenge;
		std::int32_t m_SessionKey;

	public:
		StatsClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB);
		~StatsClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<void> SendPacket(std::string message);
		boost::asio::awaitable<bool> Authenticate(boost::asio::experimental::coro<std::span<char>>& reader);
	};
}