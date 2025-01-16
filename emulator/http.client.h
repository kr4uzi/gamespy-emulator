#pragma once
#ifndef _GAMESPY_HTTP_CLIENT_H_
#define _GAMESPY_HTTP_CLIENT_H_

#include "asio.h"

namespace gamespy
{
	class GameDB;
	class PlayerDB;
	class HttpClient
	{
		boost::asio::ip::tcp::socket m_Socket;

	public:
		HttpClient(boost::asio::ip::tcp::socket socket);
		~HttpClient();

		boost::asio::awaitable<void> Run();
	};
}

#endif