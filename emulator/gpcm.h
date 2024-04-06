#pragma once
#include "asio.h"

namespace gamespy {
	class Database;
	class LoginServer {
		static constexpr std::uint16_t PORT = 29900; // gpcm.gamespy.com
		boost::asio::ip::tcp::acceptor m_Acceptor;
		Database& m_DB;

	public:
		LoginServer(boost::asio::io_context& context, Database& db);
		~LoginServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}