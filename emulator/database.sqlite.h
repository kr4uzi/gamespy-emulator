#pragma once
#include "database.h"
#include <map>
#include <memory>

namespace sqlite {
	struct sqlite3;
}

namespace gamespy {
	class SQLite3DB : public Database
	{
	private:
		typedef std::unique_ptr<sqlite::sqlite3, void(*)(sqlite::sqlite3*)> db_ptr;

		std::map<std::string, GameData> m_Games;
		std::string m_LastError;
		db_ptr m_DB;

	public:
		SQLite3DB(SQLite3DB&&) = default;
		SQLite3DB& operator=(SQLite3DB&&) = default;

		SQLite3DB(const std::string& sqlite3File, std::map<std::string, GameData> games = {});
		~SQLite3DB();

		virtual bool HasGame(const std::string_view& name) noexcept;
		virtual GameData GetGame(const std::string_view& name);

		virtual bool HasError() const noexcept;
		virtual std::string GetError() const noexcept;
		virtual void ClearError() noexcept;

		virtual bool HasPlayer(const std::string_view& name) noexcept;
		virtual PlayerData GetPlayerByName(const std::string_view& name);
		virtual std::vector<PlayerData> GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) noexcept;
		virtual void CreatePlayer(PlayerData& data);
		virtual void UpdatePlayer(const PlayerData& data);
	};
}