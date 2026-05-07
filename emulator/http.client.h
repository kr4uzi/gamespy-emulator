#pragma once
#ifndef _GAMESPY_HTTP_CLIENT_H_
#define _GAMESPY_HTTP_CLIENT_H_

#include "asio.h"
#include <boost/url/url_view.hpp>

namespace gamespy
{
	class GameDB;
	class PlayerDB;
	class HttpClient
	{
		boost::asio::ip::tcp::socket m_Socket;
		GameDB& m_GameDB;
		PlayerDB& m_PlayerDB;

	public:
		HttpClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB);
		~HttpClient();

		boost::asio::awaitable<void> Run();

	private:
		boost::asio::awaitable<std::string> HandleBF2Web(const boost::urls::url_view& uri);
	};
}

#endif