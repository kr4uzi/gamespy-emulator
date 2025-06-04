#include "playerdb.sqlite.h"
#include <print>
#include <limits>
#include <stdexcept>
#include <fstream>
#include <tuple>
using namespace gamespy;

PlayerDBSQLite::PlayerDBSQLite(const std::filesystem::path& dbFile)
	: PlayerDB{}, m_DB{ dbFile }
{
	using namespace std::string_view_literals;
	auto stmt = sqlite::stmt{ m_DB, "PRAGMA user_version" };
	if (std::tuple<std::uint32_t> version; stmt.query(version)) {
		if (std::get<0>(version) < 1) {
			m_DB.exec(R"SQL(
				BEGIN TRANSACTION;
				PRAGMA user_version = 1;
				CREATE TABLE `player` (
					`id` INTEGER PRIMARY KEY AUTOINCREMENT,
					`name` VARCHAR(32) UNIQUE NOT NULL,
					`password` VARCHAR(32) NOT NULL,
					`email` VARCHAR(64) DEFAULT NULL,
					`country` CHAR(2) NOT NULL DEFAULT 'xx',
					`lastip` VARCHAR(15) NOT NULL DEFAULT '0.0.0.0',
					`joined` INT UNSIGNED NOT NULL DEFAULT (datetime('now','localtime')),
					`lastonline` INT UNSIGNED NOT NULL DEFAULT 0,
					`rank_id` TINYINT UNSIGNED NOT NULL, -- added for compatibility with bf2stats
					`permban` BOOLEAN NOT NULL DEFAULT 0,
					`bantime` INT UNSIGNED NOT NULL DEFAULT 0,
					`online` BOOLEAN NOT NULL DEFAULT 0
				);
				UPDATE sqlite_sequence SET seq=2900000 WHERE name='player';
				INSERT INTO sqlite_sequence (name, seq) SELECT 'player', 2900000 WHERE NOT EXISTS (SELECT changes() AS change FROM sqlite_sequence WHERE change <> 0);
				END TRANSACTION;
			)SQL");
		}
	}
	else
		throw std::runtime_error{ "unable to detect sqlite database version" };
}

PlayerDBSQLite::~PlayerDBSQLite()
{

}

task<bool> PlayerDBSQLite::HasPlayer(const std::string_view& name)
{
	auto stmt = sqlite::stmt{ m_DB, "SELECT COUNT(*) FROM player WHERE name=?", name };
	std::tuple<std::uint64_t> data;
	stmt.query(data);
	co_return std::get<0>(data) > 0;
}

task<std::optional<PlayerData>> PlayerDBSQLite::GetPlayerByName(const std::string_view& name)
{
	if (name.length() > std::numeric_limits<int>::max())
		throw std::overflow_error{ "name too long" };

	auto stmt = sqlite::stmt{ m_DB, "SELECT id, email, password, country FROM player WHERE name=?", name };
	if (std::tuple<std::uint64_t, std::string_view, std::string_view, std::string_view> data; stmt.query(data)) {
		co_return PlayerData{
			std::get<0>(data),
			name,
			std::get<1>(data),
			std::get<2>(data),
			std::get<3>(data)
		};
	}

	co_return std::nullopt;
}

task<std::optional<PlayerData>> PlayerDBSQLite::GetPlayerByPID(std::uint64_t pid)
{
	auto stmt = sqlite::stmt{ m_DB, "SELECT name, email, password, country FROM player WHERE id=?", static_cast<std::int64_t>(pid) };
	if (std::tuple<std::string_view, std::string_view, std::string_view, std::string_view> data; stmt.query(data)) {
		co_return PlayerData{
			pid,
			std::get<0>(data),
			std::get<1>(data),
			std::get<2>(data),
			std::get<3>(data)
		};
	}

	co_return std::nullopt;
}

task<std::vector<PlayerData>> PlayerDBSQLite::GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password)
{
	auto players = std::vector<PlayerData>{};
	auto stmt = sqlite::stmt{ m_DB, "SELECT id, name, country FROM player WHERE email=? AND password=?", email, password };
	std::tuple<std::uint64_t, std::string_view, std::string_view> data;
	while (stmt.query(data))
		players.emplace_back(std::get<0>(data), std::get<1>(data), email, password, std::get<2>(data));

	co_return players;
}

task<void> PlayerDBSQLite::CreatePlayer(PlayerData& player)
{
	auto stmt = sqlite::stmt{ m_DB, "INSERT INTO player (name, password, email, country, rank_id) VALUES (?, ?, ?, ?, 0) RETURNING id", player.name, player.password, player.email, player.country };
	std::tuple<std::uint64_t> data;
	if (stmt.query(data))
		player.id = std::get<0>(data);

	co_return;
}

task<void> PlayerDBSQLite::UpdatePlayer(const PlayerData& player)
{
	auto stmt = sqlite::stmt{ m_DB, "UPDATE player SET password=?, email=?, country=? WHERE name=?", player.password, player.email, player.country, player.name };
	stmt.update();
	co_return;
}