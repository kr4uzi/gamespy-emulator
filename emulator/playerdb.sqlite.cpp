#include "playerdb.sqlite.h"
#include <print>
#include <limits>
#include <stdexcept>
#include <fstream>
#include <tuple>
using namespace gamespy;

PlayerDBSQLite::PlayerDBSQLite(const params_t& params)
	: PlayerDB{}, m_DB{ params.db_file }
{
	using namespace std::string_view_literals;
	auto stmt = sqlite::stmt{ m_DB, "SELECT name FROM sqlite_master WHERE type='table' AND name='_version'" };//"SELECT version FROM _version ORDER BY updateid DESC LIMIT 1" };
	if (!stmt.query()) {
		auto sqlFile = std::fstream{ params.sql_file, std::ios::in };
		sqlFile.exceptions(std::ifstream::failbit);
		m_DB.exec(std::string{ std::istreambuf_iterator<char>{sqlFile}, std::istreambuf_iterator<char>{} });
	}
}

PlayerDBSQLite::~PlayerDBSQLite()
{

}

bool PlayerDBSQLite::HasError() const noexcept { return !m_LastError.empty(); }
std::string PlayerDBSQLite::GetError() const noexcept { return m_LastError; }
void PlayerDBSQLite::ClearError() noexcept { m_LastError.clear(); }

task<bool> PlayerDBSQLite::HasPlayer(const std::string_view& name)
{
	auto stmt = sqlite::stmt{ m_DB, "SELECT COUNT(*) FROM player WHERE name=?", name };
	std::tuple<std::uint32_t> data;
	stmt.query(data);
	co_return std::get<0>(data) > 0;
}

task<std::optional<PlayerData>> PlayerDBSQLite::GetPlayerByName(const std::string_view& name)
{
	if (name.length() > std::numeric_limits<int>::max())
		throw std::overflow_error{ "name too long" };

	auto stmt = sqlite::stmt{ m_DB, "SELECT id, email, password, country FROM player WHERE name=?", name };
	if (std::tuple<std::uint32_t, std::string_view, std::string_view, std::string_view> data; stmt.query(data)) {
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

task<std::vector<PlayerData>> PlayerDBSQLite::GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password)
{
	auto players = std::vector<PlayerData>{};
	auto stmt = sqlite::stmt{ m_DB, "SELECT id, name, country FROM player WHERE email=? AND password=?", email, password };
	std::tuple<std::uint32_t, std::string_view, std::string_view> data;
	while (stmt.query(data))
		players.emplace_back(std::get<0>(data), std::get<1>(data), email, password, std::get<2>(data));

	co_return players;
}

task<void> PlayerDBSQLite::CreatePlayer(PlayerData& player)
{
	auto stmt = sqlite::stmt{ m_DB, "INSERT INTO player (name, password, email, country, rank_id) VALUES (?, ?, ?, ?, 0) RETURNING id", player.name, player.password, player.email, player.country };
	std::tuple<std::uint32_t> data;
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