#pragma once
#ifndef _GAMESPY_BF2_H_
#define _GAMESPY_BF2_H_

#include "game.h"
#include "asio.h"
#include <boost/mysql.hpp>
#include <optional>

namespace gamespy
{
	class BF2 : public Game
	{
		boost::mysql::any_connection m_Conn;
		std::optional<boost::mysql::connect_params> m_Params;

	public:
		BF2(boost::asio::io_context& context);
		BF2(boost::asio::io_context& context, boost::mysql::connect_params params);
		~BF2();

		auto GetConnectionParams() const noexcept { return m_Params; }

		virtual task<void> Connect() override;
		virtual task<void> Disconnect() override;

		virtual task<void> AddOrUpdateServer(IncomingServer& server) override;
		virtual task<std::vector<SavedServer>> GetServers(const std::string_view& query, const std::vector<std::string_view>& fields, std::size_t limit) override;
	};
}

#endif
