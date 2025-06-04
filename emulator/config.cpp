#include <fstream>
#include <algorithm>
#include <regex>
#include "config.h"
#include <format>
#include <ranges>
#include <map>
#include <set>
#include <limits>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
using namespace gamespy;

Config::Config()
	: m_PlayerDBType{ PlayerDBType::SQLITE3 }, m_PlayerDBParameters{ std::filesystem::path{ "players.sqlite3" } },
	m_GameOPMode{ GameOPMode::ALL }, m_GameDBSource{ "static" }
{

}

Config::Config(const std::filesystem::path& path)
{
	namespace pt = boost::property_tree;
	using tpath = pt::ptree::path_type;
	pt::ptree cfg;
	pt::ini_parser::read_ini(path.string(), cfg);

	m_DNSEnabled = cfg.get<bool>("server.dns_enabled");
	
	const auto& playerDBType = cfg.get<std::string>("players.type");
	if (playerDBType == "sqlite3") {
		m_PlayerDBType = PlayerDBType::SQLITE3;
		m_PlayerDBParameters = std::filesystem::path{ cfg.get<std::string>("players.file") };
	}
	else if (playerDBType == "mysql") {
		m_PlayerDBType = PlayerDBType::MYSQL;
		m_PlayerDBParameters = MySQLParameters{
			.host = cfg.get<std::string>("players.host"),
			.port = cfg.get<std::uint16_t>("players.port"),
			.username = cfg.get<std::string>("players.user"),
			.password = cfg.get<std::string>("players.password"),
			.database = cfg.get<std::string>("players.database")
		};
	}
	else
		throw std::runtime_error{ std::format("invalid player database type: {}", playerDBType) };

	const auto& gameSource = cfg.get<std::string>("games.source");
	if (gameSource.empty())
		throw std::runtime_error{ "games.source must not be empty" };
	else if (gameSource != "static") {
		if (!std::filesystem::exists(gameSource))
			throw std::runtime_error{ std::format("games.source file does not exist: {}", gameSource) };
	}

	m_GameDBSource = gameSource;

	const auto& gameOPMode = cfg.get<std::string>("games.mode");
	if (gameOPMode == "all")
		m_GameOPMode = GameOPMode::ALL;
	else if (gameOPMode == "include")
		m_GameOPMode = GameOPMode::EXPLICIT_INCLUDE;
	else if (gameOPMode == "exclude")
		m_GameOPMode = GameOPMode::EXPLICIT_EXCLUDE;
	else
		throw std::runtime_error{ std::format("invalid game mode: {}", gameOPMode) };

	m_GameNames = cfg.get<std::string>("games.games")
		| std::views::split(',')
		| std::views::transform([](const auto& part) { return std::string{ part.begin(), part.end() }; })
		| std::ranges::to<std::vector<std::string>>();
}

Config::~Config()
{

}


void Config::CreateTemplate(const std::filesystem::path& to)
{
	auto target = std::ofstream{ to };
	target << "; Gamespy Emulator Config" << std::endl
		<< std::endl
		<< "[server]" << std::endl
		<< "dns_enabled=1" << std::endl
		<< "http_enabled=1" << std::endl
		<< std::endl
		<< std::endl
		<< "; Player/Profile Configuration" << std::endl
		<< "[players]" << std::endl
		<< "; sqlite3 or mysql" << std::endl
		<< "type=sqlite3" << std::endl
		<< std::endl
		<< "; options for sqlite3" << std::endl
		<< "file=player_db.sqlite3" << std::endl
		<< std::endl
		<< "; options for mysql" << std::endl
		<< "host=" << std::endl
		<< "port=3306" << std::endl
		<< "user=" << std::endl
		<< "password=" << std::endl
		<< "database=" << std::endl
		<< std::endl
		<< std::endl
		<< "; Game Parameter Configuration" << std::endl
		<< "[games]" << std::endl
		<< "; source={static|<file>.csv}" << std::endl
		<< "; game available in static: battlefield2" << std::endl
		<< "; if a csv file is set, the following fields are mandatory: id, name, full_name, secret_key, query_port, backend_flag, avaialble_flag, keys, key_types" << std::endl
		<< "source=static" << std::endl
		<< std::endl
		<< "; mode={all|include|exclude}" << std::endl
		<< "; games the server(s) will be available for: all, include or exclude" << std::endl
		<< "mode=include" << std::endl
		<< "; comma separated of list gamenames to load if mode={exclude|include}" << std::endl
		<< "games=battlefield2" << std::endl
		<< std::endl
	;
}

void Config::VisitGames(std::function<bool(const GameData&)> callback) const
{
	if (m_GameDBSource == "static") {
		using Available = GameData::AvailableFlag;
		using KeyType = GameData::KeyType;
		auto staticGames = std::vector<GameData> {
			{ 1059, "battlefield2", "Battlefield 2", "hW6m9a", 6500, Available::AVAILABLE, GameData::BackendFlag::NONE, false, {
				{ "hostname", KeyType::STRING },
				{ "country", KeyType::STRING },
				{ "gamename", KeyType::STRING },
				{ "gamever", KeyType::STRING },
				{ "mapname", KeyType::STRING },
				{ "gametype", KeyType::STRING },
				{ "gamevariant", KeyType::STRING },
				{ "numplayers", KeyType::SHORT },
				{ "maxplayers", KeyType::SHORT },
				{ "gamemode", KeyType::STRING },
				{ "password", KeyType::STRING },
				{ "timelimit", KeyType::STRING },
				{ "roundtime", KeyType::STRING },
				{ "hostport", KeyType::SHORT },
				{ "localip0", KeyType::STRING },
				{ "localport", KeyType::SHORT },
				{ "natneg", KeyType::BYTE },
				{ "statechanged", KeyType::BYTE },
				{ "bf2_dedicated", KeyType::STRING },
				{ "bf2_ranked", KeyType::BYTE, GameData::KeyAccess::PRIVATE },
				{ "bf2_anticheat", KeyType::BYTE },
				{ "bf2_os", KeyType::STRING },
				{ "bf2_autorec", KeyType::BYTE },
				{ "bf2_d_idx", KeyType::STRING },
				{ "bf2_d_dl", KeyType::STRING },
				{ "bf2_voip", KeyType::BYTE },
				{ "bf2_autobalanced", KeyType::BYTE },
				{ "bf2_friendlyfire", KeyType::BYTE },
				{ "bf2_tkmode", KeyType::STRING },
				{ "bf2_startdelay", KeyType::STRING },
				{ "bf2_spawntime", KeyType::STRING },
				{ "bf2_sponsortext", KeyType::STRING },
				{ "bf2_sponsorlogo_url", KeyType::STRING },
				{ "bf2_communitylogo_url", KeyType::STRING },
				{ "bf2_scorelimit", KeyType::SHORT },
				{ "bf2_ticketratio", KeyType::STRING },
				{ "bf2_teamratio", KeyType::STRING },
				{ "bf2_team1", KeyType::STRING },
				{ "bf2_team2", KeyType::STRING },
				{ "bf2_bots", KeyType::BYTE },
				{ "bf2_pure", KeyType::BYTE },
				{ "bf2_mapsize", KeyType::SHORT },
				{ "bf2_globalunlocks", KeyType::BYTE },
				{ "bf2_fps", KeyType::STRING },
				{ "bf2_plasma", KeyType::BYTE, GameData::KeyAccess::PRIVATE },
				{ "bf2_reservedslots", KeyType::SHORT },
				{ "bf2_coopbotratio", KeyType::SHORT },
				{ "bf2_coopbotcount", KeyType::SHORT },
				{ "bf2_coopbotdiff", KeyType::SHORT },
				{ "bf2_novehicles", KeyType::BYTE },
			} }
		};

		for (const auto& game : staticGames) {
			if ((m_GameOPMode == GameOPMode::EXPLICIT_INCLUDE && !std::ranges::contains(m_GameNames, game.name))
				|| (m_GameOPMode == GameOPMode::EXPLICIT_EXCLUDE && std::ranges::contains(m_GameNames, game.name)))
				continue;
				
			if (callback(game))
				break;
		}

		return;
	}

	auto keyIndices = std::map<std::string, short>{
		{ "id", -1 },
		{ "name", -1 },
		{ "description", -1 },
		{ "secret_key", -1 },
		{ "query_port", -1 },
		{ "backend_flag", -1 },
		{ "available_flag", -1 },
		{ "auto_keys", -1 },
		{ "keys", -1 },
		{ "key_types", -1 }
	};

	auto src = std::ifstream{ m_GameDBSource, std::ios::in };
	if (!src.good())
		throw std::runtime_error{ std::format("failed to open {}", m_GameDBSource) };

	if (std::string line; std::getline(src, line)) {
		short i = 0;
		for (const auto& _key : line | std::views::split(',')) {
			const auto& key = std::ranges::to<std::string>(_key);
			auto iter = keyIndices.find(key);
			if (iter != keyIndices.end()) {
				if (iter->second != -1)
					throw std::runtime_error{ std::format("key appeared multiple times: {}", key) };

				iter->second = i;
			}

			i++;
		}

		for (const auto& [key, value] : keyIndices) {
			if (value == -1)
				throw std::runtime_error{ std::format("key not found: {}", key) };
		}
	}
	else
		throw std::runtime_error{ "failed to read game source header line" };

	auto expectedGames = std::set<std::string>{ std::from_range, m_GameNames };
	for (std::string line; std::getline(src, line);) {
		const auto& parts = line | std::views::split(',') | std::views::transform([](const auto& part) { return std::string{ part.begin(), part.end() }; }) | std::ranges::to<std::vector<std::string>>();
		const auto& name = parts.at(keyIndices["gamename"]);
		if (m_GameOPMode == GameOPMode::EXPLICIT_INCLUDE) {
			if (expectedGames.empty())
				break;

			expectedGames.erase(name);
		}
		else if (m_GameOPMode == GameOPMode::EXPLICIT_EXCLUDE) {
			if (expectedGames.contains(name)) {
				expectedGames.erase(name);
				continue;
			}
		}

		const auto& description = parts.at(keyIndices["description"]);
		if (description.empty())
			throw std::runtime_error{ std::format("description must not be empty for game {}", name) };

		const auto& secretKey = parts.at(keyIndices["secret_key"]);
		if (secretKey.empty())
			throw std::runtime_error{ std::format("secret_key must not be empty for game {}", name) };

		const auto& queryPort = std::stoul(parts.at(keyIndices["query_port"]));
		if (queryPort > std::numeric_limits<std::uint16_t>::max())
			throw std::out_of_range{ std::format("invalid query_port for game {}: {}", name, queryPort) };

		const auto& _backendFlag = std::stoul(parts.at(keyIndices["backend_flag"]));
		if (_backendFlag && _backendFlag != std::to_underlying(GameData::BackendFlag::QR2_USE_QUERY_CHALLENGE))
			throw std::out_of_range{ std::format("invalid backend_flags for game {}: {}", name, _backendFlag) };
		const auto backendFlag = static_cast<GameData::BackendFlag>(_backendFlag);

		const auto& _availableFlag = std::stoul(parts.at(keyIndices["available_flag"]));
		if (_availableFlag > std::to_underlying(GameData::AvailableFlag::DISABLED_PREMANENTLY))
			throw std::out_of_range{ std::format("invalid backend_flags for game {}: {}", name, _availableFlag) };
		const auto availableFlag = static_cast<GameData::AvailableFlag>(_availableFlag);

		const auto& keyList = parts.at(keyIndices["keys"])
			| std::views::split('\\')
			| std::views::transform([](const auto& part) { return std::string{ part.begin(), part.end() }; })
			| std::ranges::to<std::vector<std::string>>();

		const auto& keyTypes = parts.at(keyIndices["key_types"])
			| std::views::split('\\')
			| std::views::transform([&name](const auto& part) {
				const auto& type = std::stoul(std::string{ part.begin(), part.end() });
				if (type > std::to_underlying(GameData::KeyType::SHORT))
					throw std::out_of_range{ std::format("invalid key-type for game {}: {}", name, type) };

				return static_cast<GameData::KeyType>(type);
			})
			| std::ranges::to<std::vector<GameData::KeyType>>();

		if (keyList.size() != keyTypes.size())
			throw std::out_of_range{ std::format("keys and key_types do not match for game {}", name) };

		auto keys = decltype(GameData::keys){};
		for (std::size_t i = 0, len = keyList.size(); i < len; i++)
			keys.emplace_back(keyList[i], keyTypes[i]);

		const auto game = GameData{
			.id = static_cast<std::uint16_t>(std::stoul(parts.at(keyIndices["id"]))),
			.name = name,
			.description = description,
			.secretKey = secretKey,
			.queryPort = static_cast<std::uint16_t>(queryPort),
			.available = availableFlag,
			.backend = backendFlag,
			.autoKeys = false,
			.keys = keys
		};

		if (callback(game))
			break;
		
		if (m_GameOPMode != GameOPMode::ALL) {
			if (expectedGames.erase(game.name) == 0)
				break;
		}
	}
}