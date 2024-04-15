#include "database.sqlite.h"
namespace sqlite {
#include <sqlite3.h>
}
#include <print>
using namespace gamespy;
using namespace sqlite;

template<>
struct std::default_delete<sqlite3_stmt> {
	void operator()(sqlite3_stmt* stmt) const noexcept {
		sqlite3_finalize(stmt);
	}
};

SQLite3DB::SQLite3DB(const std::string& fileName, std::map<std::string, GameData> games)
	: Database(), m_DB(nullptr, [](sqlite::sqlite3* db) { sqlite3_close(db); }), m_Games(std::move(games))
{
	sqlite::sqlite3* db;
	if (sqlite3_open_v2(fileName.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK) {
		sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS player (id INTEGER PRIMARY KEY, name TEXT NOT NULL, password TEXT NOT NULL, email TEXT NOT NULL, country TEXT NOT NULL)", NULL, NULL, NULL);
		m_DB.reset(db);
	}
	else
		m_LastError = sqlite3_errmsg(db);
}

SQLite3DB::~SQLite3DB()
{

}

bool SQLite3DB::HasError() const noexcept { return !m_LastError.empty(); }
std::string SQLite3DB::GetError() const noexcept { return m_LastError; }
void SQLite3DB::ClearError() noexcept { m_LastError.clear(); }

bool SQLite3DB::HasGame(const std::string_view& name) noexcept
{
	return m_Games.find(std::string(name)) != m_Games.end();
}

GameData SQLite3DB::GetGame(const std::string_view& name)
{
	auto iter = m_Games.find(std::string(name));
	if (iter != m_Games.end())
		return iter->second;

	throw std::exception("Game not found");
}

bool SQLite3DB::HasPlayer(const std::string_view& name) noexcept
{
	bool exists = false;

	sqlite3_stmt* stmt;
	sqlite3_prepare_v2(m_DB.get(), "SELECT COUNT(*) FROM player WHERE name=?", -1, &stmt, NULL);
	if (stmt) {
		sqlite3_bind_text(stmt, 1, name.data(), name.size(), SQLITE_STATIC);

		int RC = sqlite3_step(stmt);
		if (RC == SQLITE_ROW)
			exists = sqlite3_column_int(stmt, 0) != 0;
		else
			m_LastError = sqlite3_errmsg(m_DB.get());

		sqlite3_finalize(stmt);
	}
	else
		m_LastError = sqlite3_errmsg(m_DB.get());

	return exists;
}

PlayerData SQLite3DB::GetPlayerByName(const std::string_view& name)
{
	sqlite3_stmt* stmt;
	sqlite3_prepare_v2(m_DB.get(), "SELECT id, email, password, country FROM player WHERE name=?", -1, &stmt, NULL);

	if (stmt) {
		std::unique_ptr<sqlite3_stmt> guard(stmt);
		sqlite3_bind_text(stmt, 1, name.data(), name.length(), SQLITE_STATIC);
		int RC = sqlite3_step(stmt);

		if (RC == SQLITE_ROW) {
			return PlayerData(
				static_cast<std::uint32_t>(sqlite3_column_int(stmt, 0)),
				name,
				reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
				reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
				reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
			);
		}
	}
	
	m_LastError = sqlite3_errmsg(m_DB.get());
	throw std::exception("Failed to get game");
}

std::vector<PlayerData> SQLite3DB::GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) noexcept
{
	std::vector<PlayerData> players;

	sqlite3_stmt* stmt;
	sqlite3_prepare_v2(m_DB.get(), "SELECT id, name, country FROM player WHERE email=? AND password=?", -1, &stmt, NULL);

	if (stmt) {
		std::unique_ptr<sqlite3_stmt> guard(stmt);

		sqlite3_bind_text(stmt, 1, email.data(), email.length(), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, password.data(), password.length(), SQLITE_STATIC);
		int RC = sqlite3_step(stmt);

		while (RC == SQLITE_ROW) {
			std::uint32_t id = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 0));
			PlayerData data(id, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)), email, password, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
			players.push_back(std::move(data));
			RC = sqlite3_step(stmt);
		}

		if (RC == SQLITE_ERROR)
			m_LastError = sqlite3_errmsg(m_DB.get());
	}
	else
		m_LastError = sqlite3_errmsg(m_DB.get());

	return players;
}

void SQLite3DB::CreatePlayer(PlayerData& data)
{
	sqlite3_stmt* stmt;
	sqlite3_prepare_v2(m_DB.get(), "INSERT INTO player (name, password, email, country) VALUES ( ?, ?, ?, ? ) RETURNING id", -1, &stmt, NULL);

	if (stmt) {
		std::unique_ptr<sqlite3_stmt> guard(stmt);

		sqlite3_bind_text(stmt, 1, data.name.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, data.password.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 3, data.email.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 4, data.country.c_str(), -1, SQLITE_STATIC);

		int RC = sqlite3_step(stmt);
		if (RC == SQLITE_ROW)
			data.id = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 0));
		else
			m_LastError = sqlite3_errmsg(m_DB.get());
	}
	else
		m_LastError = sqlite3_errmsg(m_DB.get());
}

void SQLite3DB::UpdatePlayer(const PlayerData& data)
{
	sqlite3_stmt* stmt;
	sqlite3_prepare_v2(m_DB.get(), "UPDATE player SET password=?, email=?, country=? WHERE name=?", -1, &stmt, NULL);

	if (stmt) {
		std::unique_ptr<sqlite3_stmt> guard(stmt);
		sqlite3_bind_text(stmt, 1, data.password.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, data.email.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, data.country.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 4, data.name.c_str(), -1, SQLITE_TRANSIENT);

		int RC = sqlite3_step(stmt);
		if (RC == SQLITE_OK)
			return;

	}
	
	m_LastError = sqlite3_errmsg(m_DB.get());
}