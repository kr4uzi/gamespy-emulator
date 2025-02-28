#pragma once
#include "asio.h"
#include "config.h"

namespace gamespy {
	class HttpServer {
		boost::asio::ip::tcp::acceptor m_Acceptor;

	public:
		HttpServer(boost::asio::io_context& context);
		~HttpServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}