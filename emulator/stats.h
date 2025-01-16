#pragma once
#include "asio.h"

namespace gamespy {
	class GameDB;
	class StatsServer {
		static constexpr std::uint16_t PORT = 29920; // gamestats.gamespy.com, *s.gamestats.gamespy.com
		boost::asio::ip::tcp::acceptor m_Acceptor;
		GameDB& m_DB;

	public:
		StatsServer(boost::asio::io_context& context, GameDB& db);
		~StatsServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}