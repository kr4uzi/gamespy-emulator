#pragma once
#ifndef _GAMESPY_GAME_DB_H_
#define _GAMESPY_GAME_DB_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <functional>
#include <optional>
#include <chrono>
#include <array>
#include <boost/signals2/signal.hpp>
#include "task.h"
#include "sqlite.h"
#include "config.h"

namespace gamespy {
	using Clock = std::chrono::system_clock;

	class Game {
	private:
		sqlite::db m_DB;
		GameData m_Data;
		std::map<std::string_view, const GameData::GameKey*> m_Params; // references to m_Data.keys

		// up to 254 most used values (implemented for completeness - was this used to save bandwith?)
		std::vector<std::string> m_PopularValues;

	public:
		Game(GameData data);
		std::string GetMasterServer() const; // calculates the designated master server (%s.ms%d.gamespy.com) for this game

		const GameData& Data() const noexcept { return m_Data; }

		static bool IsValidParamName(const std::string& paramName);

		// when sending server data via key-value pairs to the clients,
		// the list of popular-keys is first sent.
		// they are then used as a references to keys:
		// instead of sending the full key-string, only the index of the key within the popularKey array is sent
		// Note: No more than 254 keys must be stored (otherwise runtime overflow-exception will be thrown!)
		const std::vector<std::string>& GetPopularValues() const noexcept { return m_PopularValues; }

		template<typename R> requires std::ranges::range<R>
		void SetPopularValues(R&& popularValues)
		{
			if (std::ranges::size(popularValues) > 254)
				throw std::overflow_error("No more than 254 popular values are allowed!");

			m_PopularValues.clear();
			m_PopularValues.append_range(popularValues);
		}

		struct Server {
			std::chrono::time_point<Clock> last_update;
			const std::string public_ip;
			const std::uint16_t public_port;
			std::string private_ip;
			std::uint16_t private_port = 0;
			std::string icmp_ip;
			std::map<std::string, std::string> data;
			std::string stats; // gamespy calls those "RULES"
		};

		task<void> AddOrUpdateServer(Server& server);
		std::vector<Server> GetServers(const std::string& query, const std::vector<std::string>& fields, const std::size_t limit);
		void CleanupServers(const std::vector<std::pair<std::string, std::uint16_t>>& servers);

		boost::signals2::signal<void(Game::Server& server)> BeforeServerAdd;
	};

	class GameDB
	{
	public:
		GameDB();
		virtual ~GameDB();

		virtual bool HasGame(const std::string& name) = 0;
		virtual Game& GetGame(const std::string& name) = 0;
	};

	class GameDBSQLite : public GameDB
	{
		std::map<std::string, Game> m_Games;

	public:
		GameDBSQLite(const Config& config);
		~GameDBSQLite();

		virtual bool HasGame(const std::string& name) override;
		virtual Game& GetGame(const std::string& name) override;
	};
}
#endif