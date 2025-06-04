#pragma once
#include "playerdb.h"
#include "sqlite.h"

namespace gamespy {
	class PlayerDBSQLite : public PlayerDB
	{
		sqlite::db m_DB;
		std::string m_LastError;

	public:
		PlayerDBSQLite(const std::filesystem::path& dbFile);
		~PlayerDBSQLite();

		virtual task<bool> HasPlayer(const std::string_view& name) override;
		virtual task<std::optional<PlayerData>> GetPlayerByName(const std::string_view& name) override;
		virtual task<std::optional<PlayerData>> GetPlayerByPID(std::uint64_t pid) override;
		virtual task<std::vector<PlayerData>> GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) override;
		virtual task<void> CreatePlayer(PlayerData& data) override;
		virtual task<void> UpdatePlayer(const PlayerData& data) override;
	};
}