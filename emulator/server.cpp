#include "server.h"
#include "config.h"
#include "playerdb.sqlite.h"
#include "playerdb.mysql.h"
#include "gamedb.h"
#include "master.h"
#include "gpcm.h"
#include "gpsp.h"
#include "ms.h"
#include "key.h"
#include "stats.h"
#include "http.h"
#include "asio.h"
#include "dns.h"
#include <format>
#include <memory>
#include <print>
#include <iostream>
using namespace gamespy;

class coro_log
{
	std::string m_Name;

public:
	coro_log(std::string name)
		: m_Name(std::move(name))
	{

	}

	void operator()(std::exception_ptr ex)
	{
		if (ex) {
			try {
				std::rethrow_exception(ex);
			}
			catch (std::exception& e) {
				std::println(std::cerr, "[{}][exception] {}", m_Name, e.what());
			}
		}
	}
};

Server::Server(Config& cfg, boost::asio::io_context& context)
	: m_CFG(cfg), m_Context(context)
{

}

Server::~Server()
{

}

boost::asio::awaitable<void> Server::Run()
{
	auto playerDB = co_await InitPlayerDB();
	auto gameDB = std::make_unique<gamespy::GameDBSQLite>(m_CFG);

	auto master = gamespy::MasterServer{ m_Context, *gameDB };
	auto gpcm = gamespy::LoginServer{ m_Context, *playerDB };
	auto gpsp = gamespy::SearchServer{ m_Context, *playerDB };
	auto ms = gamespy::BrowserServer{ m_Context, *gameDB };
	auto key = gamespy::CDKeyServer{ m_Context };
	auto stats = gamespy::StatsServer{ m_Context, *gameDB };
	std::unique_ptr<gamespy::DNSServer> dns;
	std::unique_ptr<gamespy::HttpServer> http;

	if (m_CFG.IsHTTPEnabled())
		http = std::make_unique<gamespy::HttpServer>(m_Context);

	if (m_CFG.IsDNSEnabled())
		dns = std::make_unique<gamespy::DNSServer>(m_Context, *gameDB);

	boost::asio::co_spawn(m_Context, master.AcceptConnections(), coro_log("master"));
	boost::asio::co_spawn(m_Context, gpcm.AcceptClients(), coro_log("login"));
	boost::asio::co_spawn(m_Context, gpsp.AcceptClients(), coro_log("search"));
	boost::asio::co_spawn(m_Context, ms.AcceptClients(), coro_log("browser"));
	boost::asio::co_spawn(m_Context, key.AcceptConnections(), coro_log("cd-key"));
	boost::asio::co_spawn(m_Context, stats.AcceptClients(), coro_log("stats"));

	if (dns)
		boost::asio::co_spawn(m_Context, dns->AcceptConnections(), coro_log("dns"));
	if (http)
		boost::asio::co_spawn(m_Context, http->AcceptClients(), coro_log("http"));

	boost::asio::system_timer wait{ m_Context, gamespy::Clock::time_point::max() };
	co_await wait.async_wait(boost::asio::use_awaitable);
}

boost::asio::awaitable<std::unique_ptr<PlayerDB>> Server::InitPlayerDB()
{
	const auto& dbType = m_CFG.GetPlayerDBType();
	const auto& dbParams = m_CFG.GetPlayerDBParameters();
	if (dbType == Config::PlayerDBType::SQLITE3)
		co_return std::make_unique<gamespy::PlayerDBSQLite>(std::get<std::filesystem::path>(dbParams));
	else if (dbType == Config::PlayerDBType::MYSQL) {
		const auto& mysqlParams = std::get<Config::MySQLParameters>(dbParams);
		auto mysqlDB = std::make_unique<gamespy::PlayerDBMySQL>(m_Context);
		co_await mysqlDB->Connect({
			.hostname = mysqlParams.host,
			.port = mysqlParams.port,
			.username = mysqlParams.username,
			.password = mysqlParams.password,
			.database = mysqlParams.database
		});

		co_return mysqlDB;
	}

	throw new std::runtime_error{ std::format("invalid player_db value: {}", (int)dbType) };
}