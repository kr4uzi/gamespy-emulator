#pragma once
#ifndef _GAMESPY_HTTP_CLIENT_H_
#define _GAMESPY_HTTP_CLIENT_H_

#include "asio.h"

namespace gamespy
{
	class GameDB;
	class HttpClient
	{
		boost::asio::ip::tcp::socket m_Socket;
		//sqlite::db& m_DB;
		GameDB& m_DB;

	public:
		HttpClient(boost::asio::ip::tcp::socket socket, GameDB& db);
		~HttpClient();

		boost::asio::awaitable<void> Run();
	};
}

#endif