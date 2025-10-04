#pragma once
#ifndef _GAMESPY_ADMIN_H_
#define _GAMESPY_ADMIN_H_

#include "asio.h"
#include <string>

namespace gamespy
{
	class GameDB;
	class PlayerDB;
	class AdminServer
	{
		boost::asio::ip::tcp::acceptor m_Acceptor;
		GameDB& m_GameDB;
		PlayerDB& m_PlayerDB;
		std::string m_Auth;

	public:
		AdminServer(boost::asio::io_context& context, GameDB& gameDB, PlayerDB& playerDB, const std::string& username, const std::string& password, boost::asio::ip::port_type port = 8081);
		~AdminServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}

#endif
