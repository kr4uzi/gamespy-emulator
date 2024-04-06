#pragma once
#include "utils.h"
#include "asio.h"

namespace gamespy {
	class Database;

	class SearchServer {
		static constexpr boost::asio::ip::port_type PORT = 29901; // gpsp.gamespy.com
		boost::asio::ip::tcp::acceptor m_Acceptor;
		Database& m_DB;

	public:
		SearchServer(boost::asio::io_context& context, Database& db);
		~SearchServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}