#include "game.h"
#include <print>
#include <iostream>
using namespace gamespy;

namespace {
	constexpr auto gamespy_num_master_servers = 20;
	constexpr auto gamespy_max_registered_keys = 254;
}

Game::Game(GameData data, bool autoKeys)
	: m_DB{ data.name, false }, m_Data{ std::move(data) }, m_AddMissingParams(autoKeys)
{
	if (std::size(m_Data.keys) > ::gamespy_max_registered_keys)
		throw std::range_error{ "too many keys" };
}

Game::~Game()
{

}

task<void> Game::Connect()
{
	// note: no sql injection prevention for config (static or via ini-file) based initalization
	std::string sql = R"SQL(
		CREATE TABLE server(
			__last_update DATETIME DEFAULT (unixepoch()),
			__public_ip TEXT NOT NULL,
			__public_port INTEGER NOT NULL
	)SQL";

	for (const auto& key : m_Data.keys) {
		if (!IsValidParamName(key.name))
			throw std::runtime_error{ std::format("parameter name {} is not allowed", key.name) };

		m_Params.emplace(key.name, &key);
		using StoreType = decltype(key.store);
		switch (key.store) {
		case StoreType::as_text:
			sql += std::format(",{} TEXT", key.name);
			break;
		case StoreType::as_real:
			sql += std::format(",{} REAL", key.name);
			break;
		case StoreType::as_integer:
			sql += std::format(",{} INTEGER", key.name);
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
		hashcode = hashcode * PRIME + std::tolower(c);

	return std::format("{}.ms{}.gamespy.com", m_Data.name, hashcode % ::gamespy_num_master_servers);
}

bool Game::IsValidParamName(const std::string_view& paramName)
{
	for (const auto& c : paramName) {
		if (std::isspace(c))
			return false;
	}

	return !paramName.starts_with("__");
}

auto Game::GetParamSendType(const std::string_view& keyName) const -> KeyType::Send
{
	auto iter = m_Params.find(keyName);
	if (iter == m_Params.end())
		return KeyType::Send::as_string;

	return iter->second->send;
}

auto Game::GetParamStoreType(const std::string_view& keyName) const -> KeyType::Store
{
	auto iter = m_Params.find(keyName);
	if (iter == m_Params.end())
		return KeyType::Store::as_text;

	return iter->second->store;
}

task<void> Game::AddOrUpdateServer(IncomingServer& server)
{
	if (server.public_ip.empty() || server.public_port == 0)
		throw std::runtime_error{ "server missing public_ip and/or public_port" };

	// add missing keys (as columns) to the database (if auto keys is active)
	auto insertSQL = std::string{ "INSERT OR REPLACE INTO server (__public_ip, __public_port" };
	std::vector<std::string_view> columnsToAdd;
	std::vector<std::string_view> valuesToInsert;
	for (const auto& [key, value] : server.data) {
		auto insertKeyValue = false;
		if (m_Params.contains(key))
			insertKeyValue = true;
		else if (m_AddMissingParams) {
			if (!IsValidParamName(key))
				throw std::runtime_error{ std::format("illegal column name: {}", key) };

			columnsToAdd.push_back(key);
		}

		if (insertKeyValue) {
			insertSQL += std::format(",{}", key);
			valuesToInsert.push_back(value);
		}
	}

	insertSQL += ") VALUES(?,?"; // first two values: public ip + port 
	for (std::size_t i = 0, len = valuesToInsert.size(); i < len; i++)
		insertSQL += ",?";
	insertSQL += ')';

	if (m_AddMissingParams && !columnsToAdd.empty()) {
		auto columnSQL = std::string{};
		for (const auto& column : columnsToAdd) {
			std::println("[gamedb][{}] new column: {}", m_Data.name, column);
			columnSQL += std::format("ALTER TABLE server ADD COLUMN {} TEXT DEFAULT '';", column);
		}

		auto guard = m_DB.set_scoped_authorizer([&](auto action, auto detail1, auto detail2, auto dbName, auto trigger) {
			using auth_action = sqlite::auth_action;
			using auth_res = sqlite::auth_res;
			if (action == auth_action::SQLITE_ALTER_TABLE && (detail1 == "temp" || detail1 == "main") && detail2 == "server")
				return auth_res::SQLITE_OK;
			else if (action == auth_action::SQLITE_FUNCTION
				&& (detail2 == "printf" || detail2 == "substr" || detail2 == "length"))
				return auth_res::SQLITE_OK;
			else if (action == auth_action::SQLITE_READ
				&& (detail1 == "sqlite_temp_master" || detail1 == "sqlite_master")
				&& (detail2 == "sql" || detail2 == "temp" || detail2 == "name" || detail2 == "type")
				&& (dbName == "temp" || dbName == "main"))
				return auth_res::SQLITE_OK;
			else if (action == auth_action::SQLITE_UPDATE
				&& (detail1 == "sqlite_temp_master" || detail1 == "sqlite_master")
				&& detail2 == "sql"
				&& (dbName == "temp" || dbName == "main"))
				return auth_res::SQLITE_OK;

			std::println(std::cerr, "[{}][columns] unauthorized: action({}) detail1({}), detail2({}), db({}), trigger({})", name(), std::to_underlying(action), detail1, detail2, dbName, trigger);
			// this will make the stmt throw an error
			return sqlite::auth_res::SQLITE_DENY;
		});

		m_DB.exec(columnSQL);

		for (const auto& column : columnsToAdd) {
			const auto& key = m_Data.keys.emplace_back(std::string(column));
			m_Params.emplace(key.name, &key);
		}
	}

	auto stmt = sqlite::stmt{ m_DB, insertSQL };
	stmt.bind_at(1, server.public_ip);
	stmt.bind_at(2, server.public_port);

	for (decltype(valuesToInsert)::size_type i = 0, size = valuesToInsert.size(); i < size; i++)
		stmt.bind_at(i + 3, valuesToInsert[i]);

	stmt.insert();

	// required to make this a coroutine
	co_return;
}

task<std::vector<Game::SavedServer>> Game::GetServers(const std::string_view& query, const std::vector<std::string_view>& fields, std::size_t limit)
{
	auto error = std::string{};

	auto sql = std::string{ "SELECT __last_update,__public_ip,__public_port" };
	for (const auto& field : fields)
		sql += std::format(",{}", field);

	// the scoped authorizer prevents SQL injection
	auto guard = m_DB.set_scoped_authorizer([&](auto action, auto detail1, auto detail2, auto dbName, auto trigger) {
		using auth_action = sqlite::auth_action;
		using auth_res = sqlite::auth_res;
		if (action == auth_action::SQLITE_READ && detail1 == "server" && (dbName == "temp" || dbName == "main"))
			return auth_res::SQLITE_OK;
		else if (action == auth_action::SQLITE_SELECT)
			return auth_res::SQLITE_OK;
		else if (action == auth_action::SQLITE_FUNCTION && (detail2 == "like"))
			return auth_res::SQLITE_OK;

		std::println(std::cerr, "[{}][retrieve] unauthorized: action({}) detail1({}), detail2({}), db({}), trigger({})", name(), std::to_underlying(action), detail1, detail2, dbName, trigger);
		return auth_res::SQLITE_DENY;
	});

	sql += " FROM server";
	if (!query.empty())
		sql += std::format(" WHERE {}", query);

	sql += std::format(" LIMIT {}", limit);

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

			for (std::size_t i = 0; i < fields.size(); i++)
				server.data.emplace(fields[i], stmt.column_at<std::string>(i + 3));

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
	}

	co_return;
}

namespace {
	// verify the gamespy invariants at the bottom of the file so it doesn't pollute the rest of the file
#include <GameSpy/serverbrowsing/sb_internal.h>
	static_assert(::gamespy_num_master_servers == NUM_MASTER_SERVERS, "NUM_MASTER_SERVERS value missmatch");
	static_assert(::gamespy_max_registered_keys == MAX_REGISTERED_KEYS, "MAX_REGISTERED_KEYS value missmatch");
}
