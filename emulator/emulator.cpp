#include "emulator.h"
#include "playerdb.sqlite.h"
#include "playerdb.mysql.h"
#include "gamedb.h"
#include "master.h"
#include "gpcm.h"
#include "gpsp.h"
#include "ms.h"
#include "key.h"
#include "stats.h"
#include <print>
#include <iostream>
#include <fstream>
#include <boost/asio/experimental/awaitable_operators.hpp>

using namespace gamespy;

Emulator::Emulator(boost::asio::io_context& context)
	: m_Context(context)
{

}

Emulator::~Emulator()
{

}

task<void> Emulator::Launch(int argc, char* argv[])
{
	for (int i = 0; i < argc; i++) {
		auto arg = std::string_view{ argv[i] };
		if (arg == "-h" || arg == "/?" || arg == "--help") {
			std::println("Allowed options:");
			std::println("-h [--help]              : produces this message");
			std::println("-gamedb=<stdin|path>     : use game-database (json) from standard input or filepath");
			std::println("-playerdb=<sqlite3-file> : use given file as player database");
			std::println("-playerdb-host           : mysql hostname for player database (bf2stats compatible)");
			std::println("-playerdb-port           : mysql port (default: 3306)");
			std::println("-playerdb-username       : mysql username for player database");
			std::println("-playerdb-password       : the user's password");
			std::println("-playerdb-database       : the database name for the player database");
			co_return;
		}
	}

	co_await InitGameDB(argc, argv);
	co_await InitPlayerDB(argc, argv);

	m_MasterServer = std::make_unique<MasterServer>(m_Context, *m_GameDB);
	m_LoginServer = std::make_unique<LoginServer>(m_Context, *m_GameDB, *m_PlayerDB);
	m_SearchServer = std::make_unique<SearchServer>(m_Context, *m_PlayerDB);
	m_BrowserServer = std::make_unique<BrowserServer>(m_Context, *m_GameDB);
	// cd-key server doesn't need db support as we accept all keys
	m_CDKeyServer = std::make_unique<CDKeyServer>(m_Context);
	m_StatsServer = std::make_unique<StatsServer>(m_Context, *m_GameDB, *m_PlayerDB);

	using namespace boost::asio::experimental::awaitable_operators;
	auto wrap = [](const std::string_view& name, task<void>&& coro) -> task<void>
	{
		try {
			co_await std::move(coro);
		}
		catch (const std::exception& e)
		{
			std::println(std::cerr, "[{}][fatal] {}", name, e.what());
		}
	};

	co_await (
		wrap("master", m_MasterServer->AcceptConnections())
		&& wrap("login", m_LoginServer->AcceptClients())
		&& wrap("search", m_SearchServer->AcceptClients())
		&& wrap("browser", m_BrowserServer->AcceptClients())
		&& wrap("cd-key", m_CDKeyServer->AcceptConnections())
		&& wrap("stats", m_StatsServer->AcceptClients())
	);

	co_await m_GameDB->Disconnect();
	co_await m_PlayerDB->Disconnect();
}

task<void> Emulator::InitGameDB(int argc, char* argv[])
{
	for (int i = 0; i < argc; i++) {
		auto arg = std::string_view{ argv[i] };
		if (arg.starts_with("-gamedb=")) {
			auto filename = arg.substr(8);
			if (filename == "stdin") {
				std::ostringstream oss;
				oss << std::cin.rdbuf();  // Blocks until EOF (Ctrl+D on Unix, Ctrl+Z on Windows)
				m_GameDB = std::make_unique<GameDBInMemory>(m_Context, nlohmann::json::parse(oss.str()));
			}
			else {
				auto file = std::ifstream{ filename.data() };
				m_GameDB = std::make_unique<GameDBInMemory>(m_Context, nlohmann::json::parse(file));
			}

			break;
		}
	}

	if (!m_GameDB)
		throw std::runtime_error{ "no game database configured. please use -gamedb=<file>" };

	co_await m_GameDB->Connect();
}

task<void> Emulator::InitPlayerDB(int argc, char* argv[])
{
	auto playerDBParams = PlayerDBMySQL::ConnectionParams{};
	for (int i = 0; i < argc; i++) {
		auto arg = std::string_view{ argv[i] };
		if (arg.starts_with("-playerdb=")) {
			m_PlayerDB = std::make_unique<PlayerDBSQLite>(arg.substr(10));
			break;
		}
		else if (arg.starts_with("-playerdb-")) {
			if (arg.starts_with("-playerdb-host="))
				playerDBParams.hostname = arg.substr(15);
			else if (arg.starts_with("-playerdb-port="))
				playerDBParams.port = std::atoi(arg.substr(15).data());
			else if (arg.starts_with("-playerdb-username="))
				playerDBParams.username = arg.substr(19);
			else if (arg.starts_with("-playerdb-password="))
				playerDBParams.password = arg.substr(19);
			else if (arg.starts_with("-playerdb-database="))
				playerDBParams.database = arg.substr(19);
		}
	}

	if (!m_PlayerDB && !playerDBParams.hostname.empty()) {
		auto mysqlDB = std::make_unique<gamespy::PlayerDBMySQL>(m_Context, std::move(playerDBParams));
		m_PlayerDB = std::move(mysqlDB);
	}

	if (!m_PlayerDB) {
		std::println("[emulator] no playerdb configured, using players.sqlite3");
		m_PlayerDB = std::make_unique<PlayerDBSQLite>("players.sqlite3");
	}

	co_await m_PlayerDB->Connect();
}
