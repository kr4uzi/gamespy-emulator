#pragma once
#ifndef _GAMESPY_KEY_H_
#define _GAMESPY_KEY_H_
#include "asio.h"
namespace gamespy {
	class CDKeyServer
	{
		static constexpr std::uint16_t PORT = 29910;

		boost::asio::ip::udp::socket m_Socket;

	public:
		CDKeyServer(boost::asio::io_context& context);
		~CDKeyServer();

		boost::asio::awaitable<void> AcceptConnections();

	private:
		boost::asio::awaitable<void> HandleKeyRequest();
	};
}
#endif