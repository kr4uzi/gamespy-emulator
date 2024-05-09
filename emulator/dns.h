#pragma once
#ifndef _GAMESPY_DNS_H_
#define _GAMESPY_DNS_H_

#include "asio.h"
namespace gamespy
{
	class GameDB;
	class DNSServer
	{
		static constexpr boost::asio::ip::port_type PORT = 53;
		boost::asio::ip::udp::socket m_Socket;
		GameDB& m_DB;

	public:
		DNSServer(boost::asio::io_context& context, GameDB& db);
		~DNSServer();

		boost::asio::awaitable<void> AcceptConnections();
	};
}

#endif
