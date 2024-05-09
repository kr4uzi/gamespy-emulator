#pragma once
#include "asio.h"

namespace gamespy {
	class GameDB;

	class HttpServer {
		boost::asio::ip::tcp::acceptor m_Acceptor;
		GameDB& m_DB;

	public:
		HttpServer(boost::asio::io_context& context,GameDB& db);
		~HttpServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}