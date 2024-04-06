#pragma once
#include "asio.h"

namespace gamespy {
	class Database;
	class MasterServer;
	class BrowserServer {
		// (legacy "enctype1") runs on 28900 (which is currently not supported and support isn't planned)
		static constexpr std::uint16_t PORT = 28910; // %s.ms%d.gamespy.com
		boost::asio::ip::tcp::acceptor m_Acceptor;

		MasterServer& m_Master;
		Database& m_DB;

	public:
		BrowserServer(boost::asio::io_context& context, MasterServer& master, Database& db);
		~BrowserServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}