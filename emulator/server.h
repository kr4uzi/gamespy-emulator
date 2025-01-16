#pragma once
#ifndef _GAMESPY_SERVER_H_
#define _GAMESPY_SERVER_H_
#include "asio.h"
#include "playerdb.h"

namespace gamespy
{
	class Config;
	class Server
	{
		Config& m_CFG;
		boost::asio::io_context& m_Context;

	public:
		Server(Config& cfg, boost::asio::io_context& context);
		~Server();

		boost::asio::awaitable<void> Run();

	private:
		boost::asio::awaitable<std::unique_ptr<PlayerDB>> InitPlayerDB();
	};
}

#endif