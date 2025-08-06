#pragma once
#ifndef _GAMESPY_GAMEDATA_H_
#define _GAMESPY_GAMEDATA_H_

#include "asio.h"
#include "task.h"
#include "utils.h"
#include "sqlite.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gamespy {
	struct GameData
	{
		struct GameKey
		{
			std::string name;
			enum class Send : std::uint8_t
			{
				as_string = 0,
				as_byte,
				as_short
			} send = Send::as_string;

			enum class Store
			{
				as_integer,
				as_real,
				as_text
			} store = Store::as_text;
		};

		std::string name;
		std::string secretKey;
		std::uint16_t queryPort = 6500;

		enum class Availability : std::uint8_t {
			available = 0,
			disabled_temporary = 1,
			disabled_permanently = 2
		} availability = Availability::available;

		enum class BackendOptions : std::uint16_t {
			none = 0,
			qr2_use_query_challenge = 128,
		} backend = BackendOptions::qr2_use_query_challenge;

		std::vector<GameKey> keys;
	};

	class Game
	{
		sqlite::db m_DB; // stores added servers (in-memory)
		GameData m_Data;
		bool m_AddMissingParams = false;
		std::map<std::string_view, const GameData::GameKey*> m_Params; // references to m_Data.keys

		// the (up to) 254 most frequently used values of the key-value pairs of the sever data can be
		// stored here. instead of then sending the actual value, only the index within this array is sent.
		// the popular values are sent initially when the server list is queried
		std::vector<std::string> m_PopularValues;

	public:
		using KeyType = GameData::GameKey;

		template<typename StringType>
		struct ServerData {
			Clock::time_point last_update;
			StringType public_ip;
			std::uint16_t public_port;
			StringType private_ip;
			std::uint16_t private_port = 0;
			StringType icmp_ip;
			std::map<std::string_view, StringType> data;

			// this can be used e.g. for highscore tracking
			// (haven't really figured it out completely, but bf2 doesn't use it anyways)
			StringType rules;
		};

		using IncomingServer = ServerData<std::string_view>;
		using SavedServer = ServerData<std::string>;

		Game(GameData data, bool autoParams = false);
		virtual ~Game();

		virtual task<void> Connect();
		virtual task<void> Disconnect();

		virtual task<void> AddOrUpdateServer(IncomingServer& server);
		virtual task<std::vector<SavedServer>> GetServers(const std::string_view& query, const std::vector<std::string_view>& fields, std::size_t limit);
		virtual task<void> RemoveServers(const std::vector<std::pair<std::string_view, std::uint16_t>>& servers);

		std::string GetMasterServer() const; // calculates the designated master server (%s.ms%d.gamespy.com) for this game
		auto& GetPopularValues() const { return m_PopularValues; }
		void SetPopularValues(decltype(m_PopularValues) values) { m_PopularValues.assign_range(values); }

		auto name() const -> std::string_view { return m_Data.name; }
		auto secretKey() const { return m_Data.secretKey; }
		auto queryPort() const { return m_Data.queryPort; }
		auto availability() const { return m_Data.availability; }
		auto backend() const { return m_Data.backend; }
		auto keys() const -> const decltype(m_Data.keys)& { return m_Data.keys; }

		static bool IsValidParamName(const std::string_view& paramName);
		KeyType::Send GetParamSendType(const std::string_view& keyName) const;
		KeyType::Store GetParamStoreType(const std::string_view& keyName) const;
	};
}

#endif
