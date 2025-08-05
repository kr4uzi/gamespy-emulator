#pragma once
#ifndef _GAMESPY_GAME_DB_H_
#define _GAMESPY_GAME_DB_H_

#include "asio.h"
#include "task.h"
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string_view>

namespace gamespy {
	class Game;
	class GameDB
	{
	public:
		GameDB();
		virtual ~GameDB();

		virtual task<void> Connect() = 0;
		virtual task<void> Disconnect() = 0;

		virtual task<bool> HasGame(const std::string_view& name) = 0;
		virtual task<std::shared_ptr<Game>> GetGame(const std::string_view& name) = 0;
	};

	class GameDBInMemory : public GameDB
	{
		boost::asio::io_context& m_Context;
		std::map<std::string_view, std::shared_ptr<Game>> m_Games;
		nlohmann::json m_Config;

	public:
		GameDBInMemory(boost::asio::io_context& context, nlohmann::json config);
		~GameDBInMemory();

		virtual task<void> Connect() override;
		virtual task<void> Disconnect() override;

		virtual task<bool> HasGame(const std::string_view& name) override;
		virtual task<std::shared_ptr<Game>> GetGame(const std::string_view& name) override;

		static bool ValidateConfig(const nlohmann::json& config);
	};
}
#endif
