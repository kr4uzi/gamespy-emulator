#pragma once
#ifndef _GAMESPY_CONFIG_H_
#define _GAMESPY_CONFIG_H_

#include <variant>
#include <filesystem>
#include <string>
#include <cstdint>
#include <vector>
#include <utility>
#include <functional>

namespace gamespy {
	struct GameData
	{
		enum class KeyType : std::uint8_t {
			STRING = 0,
			BYTE = 1,
			SHORT = 2
		};

		enum class AvailableFlag : std::uint8_t {
			AVAILABLE = 0,
			DISABLED_TEMPORARY = 1,
			DISABLED_PREMANENTLY = 2
		};

		enum class BackendFlag : std::uint8_t {
			NONE = 0,
			QR2_USE_QUERY_CHALLENGE = 128,
		};

		std::uint16_t id;
		std::string name;
		std::string description;
		std::string secretKey;
		std::uint16_t queryPort;
		AvailableFlag available;
		BackendFlag backend;
		bool autoKeys; // wether to automatically add missing keys when a server is added
		std::vector<std::pair<std::string, KeyType>> keys;

		KeyType GetKeyType(const std::string& keyName) const noexcept
		{
			for (const auto& key : keys)
			{
				if (key.first == keyName)
					return key.second;
			}

			return KeyType::STRING;
		}
	};

	class Config
	{
	public:
		enum class PlayerDBType
		{
			SQLITE3,
			MYSQL
		};

		struct MySQLParameters
		{
			std::string host;
			std::uint16_t port;
			std::string username;
			std::string password;
			std::string database;
		};

		enum class GameOPMode
		{
			ALL,
			EXPLICIT_INCLUDE,
			EXPLICIT_EXCLUDE
		};

	private:
		bool m_DNSEnabled = false;
		bool m_HTTPEnabled = false;

		PlayerDBType m_PlayerDBType;
		std::variant<std::filesystem::path, MySQLParameters> m_PlayerDBParameters;

		GameOPMode m_GameOPMode;
		std::vector<std::string> m_GameNames;

		std::string m_GameDBSource;

	public:
		Config();
		Config(const std::filesystem::path& path);
		~Config();

		bool IsDNSEnabled() const noexcept { return m_DNSEnabled; }
		bool IsHTTPEnabled() const noexcept { return m_HTTPEnabled; }

		static void CreateTemplate(const std::filesystem::path& to);

		auto GetPlayerDBType() const noexcept { return m_PlayerDBType; }
		auto& GetPlayerDBParameters() const noexcept { return m_PlayerDBParameters; }

		auto GetGameOPMode() const noexcept { return m_GameOPMode; }
		auto& GetGameNames() const noexcept { return m_GameNames; }

		void VisitGames(std::function<bool(const GameData&)> callback) const;
	};
}

#endif