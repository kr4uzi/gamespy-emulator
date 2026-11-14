#pragma once
#ifndef _GAMESPY_HTTP_CLIENT_H_
#define _GAMESPY_HTTP_CLIENT_H_

#include "asio.h"
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/url/url_view.hpp>

namespace gamespy
{
	class GameDB;
	class PlayerDB;
	class HttpClient
	{
		boost::beast::tcp_stream m_Stream;
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