#pragma once
#ifndef _GAMESPY_BF2_H_
#define _GAMESPY_BF2_H_

#include "game.h"
#include "asio.h"
#include <boost/mysql.hpp>

namespace gamespy
{
	class BF2 : public Game
	{
	public:
		struct ConnectionParams
		{
			std::string hostname;
			std::uint16_t port;
			std::string username;
			std::string password;
			std::string database;
		};

	private:
		boost::asio::ssl::context m_SSL;
		boost::mysql::tcp_ssl_connection m_Conn;
		ConnectionParams m_Params;

	public:
		BF2(boost::asio::io_context& context, ConnectionParams params);
		~BF2();

		virtual task<void> Connect() override;
		virtual task<void> Disconnect() override;

		virtual task<std::vector<SavedServer>> GetServers(const std::string_view& query, const std::vector<std::string_view>& fields, std::size_t limit) override;
	};
}

#endif
