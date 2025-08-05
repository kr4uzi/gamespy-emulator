#pragma once
#ifndef _GAMESPY_PLAYERDB_MYSQL_H_
#define _GAMESPY_PLAYERDB_MYSQL_H_
#include "playerdb.h"
#include "asio.h"
//#include <boost/asio/ssl.hpp>
#include <boost/mysql.hpp>

namespace gamespy {
	class PlayerDBMySQL : public PlayerDB
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
		PlayerDBMySQL(boost::asio::io_context& context, ConnectionParams params);
		~PlayerDBMySQL();

		task<void> Connect() override;
		task<void> Disconnect() override;

		virtual task<bool> HasPlayer(const std::string_view& name) override;
		virtual task<std::optional<PlayerData>> GetPlayerByName(const std::string_view& name) override;
		virtual task<std::optional<PlayerData>> GetPlayerByPID(std::uint64_t pid) override;
		virtual task<std::vector<PlayerData>> GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) override;
		virtual task<void> CreatePlayer(PlayerData& data) override;
		virtual task<void> UpdatePlayer(const PlayerData& data) override;
	};
}
#endif