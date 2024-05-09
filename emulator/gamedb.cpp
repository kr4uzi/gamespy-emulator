#include "gamedb.h"
#include <format>
#include <regex>
#include <fstream>
#include <format>
#include <utility>
#include <ranges>
#include <print>
#include <ctime>
using namespace gamespy;

Game::Game(std::string name, std::string description, std::string secretKey, std::uint16_t queryPort, bool autoParams, std::map<std::string, Param> params)
	: m_DB{ name, false },
	m_Name{ std::move(name) }, m_Description{ std::move(description) }, m_SecretKey{ std::move(secretKey) }, m_QueryPort{ queryPort },
	m_AutoParams{ autoParams }, m_Params { std::move(params) }
{
	std::string sql = R"SQL(
		CREATE TABLE server(
			__last_update DATETIME DEFAULT (unixepoch()),
			__public_ip TEXT NOT NULL,
			__public_port INTEGER NOT NULL)SQL";

	for (const auto& [name, param] : m_Params)
		sql += std::format(",\n\t\t\t{} {} DEFAULT {}", name, param.type, param.default_value);

	sql += R"SQL(,
			PRIMARY KEY(__public_ip, __public_port)
		);
		CREATE TRIGGER server_last_updated AFTER UPDATE ON server FOR EACH ROW
		BEGIN
			UPDATE server SET __last_update=(unixepoch()) WHERE rowid=NEW.rowid;
		END;
	)SQL";

	m_DB.exec(sql);
	std::println("[{}] registered", m_Name);
}

std::string Game::GetMasterServer() const
{
	static constexpr auto PRIME = 0x9CCF9319; // same prime is also used to decode cd-keys

	auto hashcode = std::uint32_t{ 0 };
	for (const auto& c : m_Name)
		hashcode = hashcode * PRIME + std::tolower(c);

	static constexpr auto NUM_MASTER_SERVERS = 20;
	return std::format("{}.ms{}.gamespy.com", m_Name, hashcode % NUM_MASTER_SERVERS);
}

bool Game::IsValidParamName(const std::string& paramName)
{
	for (const auto& c : paramName) {
		if (std::isspace(c))
			return false;
	}

	return !paramName.starts_with("__");
}

void Game::AddOrUpdateServer(Server& server)
{
	if (server.public_ip.empty() || server.public_port == 0)
		throw std::runtime_error{ "server missing public_ip and/or public_port" };

	BeforeServerAdd(server);

	auto insertSQL = std::string{ "INSERT OR REPLACE INTO server (__public_ip, __public_port" };
	std::vector<std::string> columnsToAdd;
	std::vector<std::string_view> valuesToAdd;
	for (const auto& [key, value] : server.data) {
		if (!m_Params.contains(key)) {
			if (m_AutoParams) {
				if (!IsValidParamName(key))
					throw std::runtime_error{ std::format("illegal column name: {}", key) };
				
				columnsToAdd.push_back(key);
			}
			else
				continue;
		}
		
		insertSQL += "," + key;

		valuesToAdd.push_back(value);
	}

	insertSQL += ") VALUES(?,?";
	for (std::size_t i = 0, len = valuesToAdd.size(); i < len; i++)
		insertSQL += ",?";
	insertSQL += ')';

	if (m_AutoParams && !columnsToAdd.empty()) {
		auto columnSQL = std::string{};
		for (const auto& column : columnsToAdd) {
			std::println("[gamedb][{}] new column: {}", m_Name, column);
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

			std::println("[gamedb][{}][columns]unauthorized: action({}) detail1({}), detail2({}), db({}), trigger({})", m_Name, std::to_underlying(action), detail1, detail2, dbName, trigger);
			// this will make the stmt throw an error
			return sqlite::auth_res::SQLITE_DENY;
		});

		m_DB.exec(columnSQL);

		for (const auto& column : columnsToAdd)
			m_Params.emplace(column, Param{ .type = "TEXT" });
	}

	auto stmt = sqlite::stmt{ m_DB, insertSQL };
	stmt.bind_at(1, server.public_ip);
	stmt.bind_at(2, server.public_port);

	for (decltype(valuesToAdd)::size_type i = 0, size = valuesToAdd.size(); i < size; i++)
		stmt.bind_at(i + 3, valuesToAdd[i]);

	stmt.insert();
}

std::vector<Game::Server> Game::GetServers(const std::string& query, const std::vector<std::string>& fields, const std::size_t limit)
{
	auto error = std::string{};
	auto servers = std::vector<Game::Server>{};

	auto sql = std::string{ "SELECT __last_update,__public_ip,__public_port" };
	for (const auto& field : fields)
		sql += std::format(",{}", field);

	auto guard = m_DB.set_scoped_authorizer([&](auto action, auto detail1, auto detail2, auto dbName, auto trigger) {
		using auth_action = sqlite::auth_action;
		using auth_res = sqlite::auth_res;
		if (action == auth_action::SQLITE_READ && detail1 == "server" && (dbName == "temp" || dbName == "main"))
			return auth_res::SQLITE_OK;
		else if (action == auth_action::SQLITE_SELECT)
			return auth_res::SQLITE_OK;
		else if (action == auth_action::SQLITE_FUNCTION && (detail2 == "like"))
			return auth_res::SQLITE_OK;

		std::println("[gamedb][{}][retrieve]unauthorized: action({}) detail1({}), detail2({}), db({}), trigger({})", m_Name, std::to_underlying(action), detail1, detail2, dbName, trigger);
		return auth_res::SQLITE_DENY;
	});
	
	sql += " FROM server";
	if (!query.empty())
		sql += std::format(" WHERE {}", query);

	sql += std::format(" LIMIT {}", limit);

	auto stmt = sqlite::stmt{ m_DB, sql };
	while (stmt.query()) {
		auto lastUpdated = stmt.column_at<std::time_t>(0);
		Server server{
			.last_update = Clock::from_time_t(lastUpdated),
			.public_ip = stmt.column_at<std::string>(1),
			.public_port = stmt.column_at<std::uint16_t>(2)
		};

		for (std::size_t i = 0; i < fields.size(); i++)
			server.data.emplace(fields[i], stmt.column_at<std::string>(i + 3));

		servers.push_back(server);
	}

	return servers;
}

void Game::CleanupServers(const std::vector<std::pair<std::string, std::uint16_t>>& servers)
{
	auto stmt = sqlite::stmt{ m_DB, "DELETE FROM server WHERE __public_ip=? and __public_port=?" };
	for (const auto& [ip, port] : servers) {
		stmt.bind(ip, port);
		stmt.update();
		stmt.reset();
	}
}

GameDB::GameDB()
{

}

GameDB::~GameDB()
{

}

void GameDB::ParseGamesTSV(const std::filesystem::path& path, std::function<bool(const ParsedGame&)> callback)
{
	auto gamesFile = std::ifstream{ path, std::ios::in };
	if (!gamesFile.is_open())
		throw std::runtime_error{ std::format("Unable to find file `{}`", path.generic_string()) };

	// FULL-TITLE	GAMENAME	GAMEID	SECRETKEY	STATS_VERSION	STATS_KEY
	const auto pattern = std::regex{ R"(^(.+)\t(\w+)\t(\d*)\t(\w*)\t(\d*)\t(\w*)$)" };
	auto match = std::smatch{};

	for (std::string line; std::getline(gamesFile, line);) {
		if (line.starts_with('#') || line.empty())
			continue;

		if (std::regex_match(line, match, pattern)) {
			auto stop = callback(ParsedGame{
				.name = { line.data() + match.position(2), static_cast<std::size_t>(match.length(2)) },
				.description = { line.data() + match.position(1), static_cast<std::size_t>(match.length(1)) },
				.secretKey = { line.data() + match.position(4), static_cast<std::size_t>(match.length(4)) },
				.queryPort = 6500
			});

			if (stop)
				return;
		}
	}
}

void GameDB::ParseServerParameters(const std::filesystem::path& path, std::function<bool(const ParsedParameter&)> callback)
{
	auto gamesFile = std::ifstream{ path, std::ios::in };
	if (!gamesFile.is_open())
		throw std::runtime_error{ std::format("Unable to find file `{}`", path.generic_string()) };

	// GAMEID PARAMETER
	const auto pattern = std::regex{ R"(^(\w+)\s+(\w+)\s+(\w+)\s+(\S+)$)" };
	auto match = std::smatch{};

	for (std::string line; std::getline(gamesFile, line);) {
		if (line.starts_with('#') || line.empty())
			continue;

		if (std::regex_match(line, match, pattern)) {
			auto stop = callback(ParsedParameter{
				.game = { line.data() + match.position(1), static_cast<std::size_t>(match.length(1)) },
				.name = { line.data() + match.position(2), static_cast<std::size_t>(match.length(2)) },
				.type = { line.data() + match.position(3), static_cast<std::size_t>(match.length(3)) },
				.default_value = { line.data() + match.position(4), static_cast<std::size_t>(match.length(4)) }
			});

			if (stop)
				return;
		}
		else
			std::println("[GameDB][{}] invalid parameter line: {}", path.string(), line);
	}
}

GameDBSQLite::GameDBSQLite(const params_t& params)
	: GameDB{}
{
	auto games = std::set<std::string>{};
	auto serverParams = std::map<std::string, std::map<std::string, Game::Param>>{};
	GameDB::ParseServerParameters(params.game_params_file, [&](const auto& param) {
		auto game = std::string{ param.game };
		games.insert(game);
		serverParams[game].emplace(
			std::string{ param.name },
			Game::Param{
				.type = std::string{ param.type },
				.default_value = std::string{ param.default_value }
			}
		);
		return false;
	});
	
	GameDB::ParseGamesTSV(params.games_list_file, [&](const auto& game) {
		if (games.empty()) return true;

		auto gameName = std::string{ game.name };
		if (!games.contains(gameName))
			return false;

		games.erase(gameName);
		auto gameParams = serverParams["global"];
		gameParams.insert_range(serverParams[gameName]);

		m_Games.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(game.name),
			std::forward_as_tuple(std::string{ game.name }, std::string{ game.description }, std::string{ game.secretKey }, game.queryPort, std::ref(params.auto_params), gameParams)
		);
		return false;
	});
}

GameDBSQLite::~GameDBSQLite()
{

}

bool GameDBSQLite::HasGame(const std::string& name)
{
	return m_Games.contains(name);
}

Game& GameDBSQLite::GetGame(const std::string& name)
{
	return m_Games.at(name);
}