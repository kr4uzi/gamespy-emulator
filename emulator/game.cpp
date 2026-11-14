#include "game.h"
#include <print>
#include <iostream>
#include <set>
using namespace gamespy;

namespace {
	// GameSpy serverbrowsing wire constants (sb_internal.h).
	constexpr auto gamespy_num_master_servers = 20;
	constexpr auto gamespy_max_registered_keys = 254;
	constexpr auto gamespy_max_popular_values = 255;
}

std::vector<GameData::GameKey> GameData::common_keys()
{
	using Send = GameKey::Send;
	using Store = GameKey::Store;
	return {
		{ "country" },
		{ "gamemode" },
		{ "gametype" },
		{ "gamever" },
		{ "hostname" },
		{ "mapname" },
		{ "maxplayers", Send::as_byte, Store::as_integer, "integer" },
		{ "numplayers", Send::as_byte, Store::as_integer, "integer" },
		{ "password", Send::as_byte, Store::as_integer, "boolean" }
	};
}

Game::Game(GameData data)
	: m_DB{ data.name, false }, m_Data{ std::move(data) }
{
	if (std::size(m_Data.keys) > ::gamespy_max_registered_keys)
		throw std::overflow_error{ "too many keys" };

	if (m_Data.secretKey.empty())
		throw std::runtime_error{ "secret key is empty" };
}

Game::~Game()
{

}

task<void> Game::Connect()
{
	decltype(m_Params) params;
	std::string sql = R"SQL(
		CREATE TABLE server(
			__last_update DATETIME DEFAULT (unixepoch()),
			__public_ip TEXT NOT NULL,
			__public_port INTEGER NOT NULL
	)SQL";

	for (const auto& key : m_Data.keys) {
		if (!IsValidParamName(key.name))
			throw std::runtime_error{ std::format("parameter name {} is not allowed", key.name) };

		if (!params.emplace(key.name, params.size()).second)
			throw std::invalid_argument{ "duplicate game key" };
		using StoreType = decltype(key.store);
		switch (key.store) {
		case StoreType::as_text:
			sql += std::format(",\"{}\" TEXT", key.name);
			break;
		case StoreType::as_real:
			sql += std::format(",\"{}\" REAL DEFAULT 0.00", key.name);
			break;
		case StoreType::as_integer:
			sql += std::format(",\"{}\" INTEGER DEFAULT 0", key.name);
			break;
		default:
			throw std::runtime_error{ std::format("parameter {} of invalid type {}", key.name, static_cast<int>(key.send)) };
		}
	}

	sql += R"SQL(
			,PRIMARY KEY(__public_ip, __public_port)
		);
		CREATE TRIGGER server_last_updated AFTER UPDATE ON server FOR EACH ROW
		BEGIN
			UPDATE server SET __last_update=(unixepoch()) WHERE rowid=NEW.rowid;
		END;
	)SQL";

	m_DB.exec(sql);
	m_Params.swap(params);
	std::println("[{}] registered - {}", name(), GetMasterServer());
	co_return;
}

task<void> Game::Disconnect()
{
	co_return;
}

// GameSpy/serverbrowsing/sb_serverlist.c:429 (StringHash)
std::string Game::GetMasterServer() const
{
	static constexpr auto PRIME = 0x9CCF9319; // same prime is also used to decode cd-keys

	auto hashcode = std::uint32_t{ 0 };
	for (const auto& c : m_Data.name)
		hashcode = hashcode * PRIME + std::tolower(static_cast<unsigned char>(c));

	return std::format("{}.ms{}.gamespy.com", m_Data.name, hashcode % ::gamespy_num_master_servers);
}

bool Game::IsValidParamName(const std::string_view& paramName)
{
	if (paramName.empty())
		return false;

	// Column names should only contain alphanumeric characters and underscores
	// Must start with a letter or underscore (not a digit)
	// Must not start with "__" (reserved for internal use)
	if (paramName.starts_with("__"))
		return false;

	// First character must be a letter or underscore
	if (!std::isalpha(static_cast<unsigned char>(paramName[0])) && paramName[0] != '_')
		return false;

	// Remaining characters must be alphanumeric or underscore
	for (const auto& c : paramName) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
			return false;
	}

	return true;
}

auto Game::GetParamSendType(const std::string_view& keyName) const -> KeyType::Send
{
	auto iter = m_Params.find(keyName);
	if (iter == m_Params.end())
		return KeyType::Send::as_string;

	return m_Data.keys[iter->second].send;
}

auto Game::GetParamStoreType(const std::string_view& keyName) const -> KeyType::Store
{
	auto iter = m_Params.find(keyName);
	if (iter == m_Params.end())
		return KeyType::Store::as_text;

	return m_Data.keys[iter->second].store;
}

task<void> Game::AddOrUpdateServer(IncomingServer& server)
{
	if (server.public_ip.empty() || server.public_port == 0)
		throw std::runtime_error{ "server missing public_ip and/or public_port" };

	// add missing keys (as columns) to the database (if auto keys is active)
	auto insertSQL = std::string{ "INSERT OR REPLACE INTO server (__public_ip, __public_port" };
	std::vector<std::string_view> columnsToAdd;
	std::vector<std::string_view> valuesToInsert;
	std::set<std::string_view, boost::beast::iless> columnNames;
	for (const auto& [key, value] : server.data) {
		if (!columnNames.insert(key).second)
			throw std::invalid_argument{ "duplicate server key (case-insensitive)" };
		auto insertKeyValue = false;
		if (m_Params.contains(key))
			insertKeyValue = true;
		else if (m_Data.misssingKeyPolicy == GameData::MissingKeyPolicy::add_as_string) {
			if (!IsValidParamName(key))
				throw std::runtime_error{ std::format("illegal column name: {}", key) };

			columnsToAdd.push_back(key);
			insertKeyValue = true;
		}
		else
			std::println("[{}] ignoring unknown key: {}", m_Data.name, key);

		if (insertKeyValue) {
			insertSQL += std::format(",\"{}\"", key);
			valuesToInsert.push_back(value);
		}
	}

	insertSQL += ") VALUES(?,?"; // first two values: public ip + port 
	for (std::size_t i = 0, len = valuesToInsert.size(); i < len; i++)
		insertSQL += ",?";
	insertSQL += ')';

	const auto insert = [&] {
		auto stmt = sqlite::stmt{ m_DB, insertSQL };
		stmt.bind_at(1, server.public_ip);
		stmt.bind_at(2, server.public_port);
		for (std::size_t i = 0; i < valuesToInsert.size(); ++i)
			stmt.bind_at(i + 3, valuesToInsert[i]);
		stmt.insert();
	};

	if (columnsToAdd.empty()) {
		insert();
	}
	else {
		if (m_Data.keys.size() + columnsToAdd.size() > gamespy_max_registered_keys)
			throw std::overflow_error{ "too many keys" };

		// Allocate replacement metadata before changing SQL. Swapping it after
		// COMMIT cannot fail, so schema, row and metadata advance together.
		auto keys = m_Data.keys;
		auto params = m_Params;
		std::string columnSQL;
		for (const auto& column : columnsToAdd) {
			const auto index = keys.size();
			keys.emplace_back(std::string(column));
			params.emplace(column, index);
			columnSQL += std::format("ALTER TABLE server ADD COLUMN \"{}\" TEXT DEFAULT '';", column);
		}

		// Names are validated and quoted; values are bound parameters. Only the
		// user-supplied query expression needs a restricted SQL authorizer.
		m_DB.exec("BEGIN");
		try {
			m_DB.exec(columnSQL);
			insert();
			m_DB.exec("COMMIT");
		}
		catch (...) {
			m_DB.exec("ROLLBACK");
			throw;
		}
		m_Data.keys.swap(keys);
		m_Params.swap(params);
	}

	OnServerAdded(server);

	// required to make this a coroutine
	co_return;
}

task<std::vector<Game::SavedServer>> Game::GetServers(const std::string_view& query, const std::vector<std::string_view>& fields, std::size_t limit, std::size_t skip)
{
	auto error = std::string{};

	auto sql = std::string{ "SELECT __last_update,__public_ip,__public_port" };
	for (const auto& field : fields) {
		if (!IsValidParamName(field))
			throw std::invalid_argument{ "invalid server field name" };
		// Qualifying the name also avoids SQLite treating an unknown quoted
		// identifier as a string literal.
		sql += std::format(",server.\"{}\"", field);
	}

	// the scoped authorizer prevents SQL injection
	auto guard = m_DB.set_scoped_authorizer([&](auto action, auto detail1, auto detail2, auto dbName, auto trigger) {
		using auth_action = sqlite::auth_action;
		using auth_res = sqlite::auth_res;
		if (action == auth_action::SQLITE_READ && detail1 == "server" && (dbName == "temp" || dbName == "main"))
			return auth_res::SQLITE_OK; // maybe we need to prevent queries on local{ip\d+|port} columns (=detail2)?
		else if (action == auth_action::SQLITE_SELECT)
			return auth_res::SQLITE_OK;
		// SQLite may revalidate the table's DEFAULT expression after ALTER TABLE.
		// Allow its unixepoch() alongside the LIKE operator used by filters.
		else if (action == auth_action::SQLITE_FUNCTION && (detail2 == "like" || detail2 == "unixepoch"))
			return auth_res::SQLITE_OK;

		std::println(std::cerr, "[{}][retrieve] unauthorized: action({}) detail1({}), detail2({}), db({}), trigger({})", name(), std::to_underlying(action), detail1, detail2, dbName, trigger);
		return auth_res::SQLITE_DENY;
	});

	sql += " FROM server";
	if (!query.empty())
		sql += std::format(" WHERE {}", query);

	sql += std::format(" LIMIT {}", limit);

	if (skip)
		sql += std::format(" OFFSET {}", skip);

	auto servers = std::vector<Game::SavedServer>{};

	try {
		auto stmt = sqlite::stmt{ m_DB, sql };
		while (stmt.query()) {
			auto lastUpdated = stmt.column_at<std::time_t>(0);
			auto server = Game::SavedServer{
				.last_update = Clock::from_time_t(lastUpdated),
				.public_ip = stmt.column_at<std::string>(1),
				.public_port = stmt.column_at<std::uint16_t>(2)
			};

			for (std::size_t i = 0, size = fields.size(); i < size; i++)
				server.data.emplace(std::string(fields[i]), stmt.column_at<std::string>(i + 3));

			servers.push_back(server);
		}
	}
	catch (const std::exception& e) {
		std::println(std::cerr, "[{}] failed to query servers (query={}): {}", name(), query, e.what());
	}

	co_return servers;
}

task<void> Game::RemoveServers(const std::vector<std::pair<std::string_view, std::uint16_t>>& servers)
{
	auto stmt = sqlite::stmt{ m_DB, "DELETE FROM server WHERE __public_ip=? and __public_port=?" };
	for (const auto& [ip, port] : servers) {
		stmt.bind(ip, port);
		stmt.update();
		stmt.reset();

		OnServerRemoved(ip, port);
	}

	co_return;
}

void Game::CheckPopularValueSize(std::size_t newSize)
{
	if (newSize > gamespy_max_popular_values)
		throw std::overflow_error{ "too many popular values" };
}
