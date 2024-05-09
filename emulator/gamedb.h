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
#include <set>
#include <chrono>
#include <array>
#include <boost/signals2/signal.hpp>
#include "task.h"
#include "sqlite.h"

namespace gamespy {
	using Clock = std::chrono::system_clock;

	class Game {
	public:
		enum class KeyType {
			STRING = 0,
			BYTE = 1,
			SHORT = 2
		};

		struct Param {
			const std::string type;
			const std::string default_value;
		};

	private:
		sqlite::db m_DB;
		const std::string m_Name;
		const std::string m_Description;
		const std::string m_SecretKey;
		const std::uint16_t m_QueryPort;
		std::map<std::string, Param> m_Params; // known parameter names
		const bool m_AutoParams; // automatically add parameters

		// up to 254 most used values (implemented for completeness - was this used to save bandwith?)
		std::vector<std::string> m_PopularValues;

		// overrides how key-values are sent to clients (if a key is not present in this map, STRING will be used)
		std::map<std::string, KeyType> m_KeyTypeOverrides;

	public:
		Game(std::string name, std::string description, std::string secretKey, std::uint16_t queryPort, bool autoParams = false, std::map<std::string, Param> params = {});
		std::string GetMasterServer() const; // calculates the designated master server (%s.ms%d.gamespy.com) for this game

		std::string_view GetName()          const noexcept { return m_Name; }
		std::string_view GetDescription()   const noexcept { return m_Description; }
		std::string_view GetSecretKey()     const noexcept { return m_SecretKey; }
		std::uint16_t    GetQueryPort()     const noexcept { return m_QueryPort; }

		static bool IsValidParamName(const std::string& paramName);

		KeyType GetKeyType(const std::string& key) const noexcept
		{
			const auto& keyTypeIter = m_KeyTypeOverrides.find(key);
			if (keyTypeIter != m_KeyTypeOverrides.end())
				return keyTypeIter->second;

			return KeyType::STRING;
		}

		void SetKeyTypeOverrides(decltype(m_KeyTypeOverrides) keyTypeOverrides) noexcept
		{
			m_KeyTypeOverrides = std::move(keyTypeOverrides);
		}

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

		void AddOrUpdateServer(Server& server);
		std::vector<Server> GetServers(const std::string& query, const std::vector<std::string>& fields, const std::size_t limit);
		void CleanupServers(const std::vector<std::pair<std::string, std::uint16_t>>& servers);

		boost::signals2::signal<void(Game::Server& server)> BeforeServerAdd;
	};

	class GameDB
	{
	public:
		struct ParsedGame
		{
			const std::string_view name;
			const std::string_view description;
			const std::string_view secretKey;
			const std::uint16_t queryPort;
		};
		static void ParseGamesTSV(const std::filesystem::path& path, std::function<bool(const ParsedGame&)> callback);

		struct ParsedParameter
		{
			const std::string_view game;
			const std::string_view name;
			const std::string_view type;
			const std::string_view default_value;
		};
		static void ParseServerParameters(const std::filesystem::path& path, std::function<bool(const ParsedParameter&)> callback);

	public:
		GameDB();
		virtual ~GameDB();

		virtual bool HasGame(const std::string& name) = 0;
		virtual Game& GetGame(const std::string& name) = 0;
	};

	class GameDBSQLite : public GameDB
	{
		std::map<std::string, Game> m_Games;

		struct params_t
		{
			const std::filesystem::path games_list_file;
			const std::filesystem::path game_params_file;
			const bool auto_params;
		};

	public:
		GameDBSQLite(const params_t& params);
		~GameDBSQLite();

		virtual bool HasGame(const std::string& name) override;
		virtual Game& GetGame(const std::string& name) override;
	};
}
#endif