#pragma once
#include "utils.h"
#include "asio.h"

namespace gamespy {
	class Sessions;
	class PlayerDB;

	// gpsp = gamespy search player
	class SearchServer {
		static constexpr boost::asio::ip::port_type PORT = 29901; // gpsp.gamespy.com
		boost::asio::ip::tcp::acceptor m_Acceptor;
		Sessions& m_Sessions;
		PlayerDB& m_DB;

	public:
		SearchServer(boost::asio::io_context& context, PlayerDB& db, Sessions& sessions);
		~SearchServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}