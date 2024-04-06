#pragma once
#include "asio.h"

namespace gamespy {
	class NewsServer {
		boost::asio::ip::tcp::acceptor m_Acceptor;

	public:
		NewsServer(boost::asio::io_context& context);
		~NewsServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}