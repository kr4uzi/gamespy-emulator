#pragma once
#include "playerdb.h"
#include "sqlite.h"

namespace gamespy {
	class PlayerDBSQLite : public PlayerDB
	{
		sqlite::db m_DB;
		std::string m_LastError;

		struct params_t
		{
			std::filesystem::path db_file;
			std::filesystem::path sql_file;
		};

	public:
		PlayerDBSQLite(const params_t& params);
		~PlayerDBSQLite();

		virtual bool HasError() const noexcept override;
		virtual std::string GetError() const noexcept override;
		virtual void ClearError() noexcept override;

		virtual task<bool> HasPlayer(const std::string_view& name) override;
		virtual task<std::optional<PlayerData>> GetPlayerByName(const std::string_view& name) override;
		virtual task<std::vector<PlayerData>> GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) override;
		virtual task<void> CreatePlayer(PlayerData& data) override;
		virtual task<void> UpdatePlayer(const PlayerData& data) override;
	};
}