#pragma once
#ifndef _GAMESPY_EMULATOR_HTTP_H_
#define _GAMESPY_EMULATOR_HTTP_H_
#include "asio.h"

namespace gamespy {
	class GameDB;
	class PlayerDB;
	
	class HttpServer {
		boost::asio::ip::tcp::acceptor m_Acceptor;
		GameDB& m_GameDB;
		PlayerDB& m_PlayerDB;

	public:
		HttpServer(boost::asio::io_context& context, GameDB& gameDB, PlayerDB& playerDB);
		~HttpServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}
#endif
