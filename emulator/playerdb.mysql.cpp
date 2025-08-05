#include "playerdb.mysql.h"
#include <print>
using namespace gamespy;

PlayerDBMySQL::PlayerDBMySQL(boost::asio::io_context& context, ConnectionParams params)
	: m_SSL{ boost::asio::ssl::context::tls_client }, m_Conn{ context, m_SSL }, m_Params{ std::move(params) }
{

}

PlayerDBMySQL::~PlayerDBMySQL()
{

}

task<void> PlayerDBMySQL::Connect()
{
	auto resolver = boost::asio::ip::tcp::resolver{ m_Conn.get_executor() };
	const auto& endpoints = co_await resolver.async_resolve(m_Params.hostname, std::to_string(m_Params.port), boost::asio::use_awaitable);
	
	auto handshakeParams = boost::mysql::handshake_params{ m_Params.username, m_Params.password, m_Params.database };
	handshakeParams.set_ssl(boost::mysql::ssl_mode::enable);
	std::println("[playerdb] connecting to {}:{} db={}", m_Params.hostname, m_Params.port, m_Params.database);
	co_await m_Conn.async_connect(*endpoints, handshakeParams, boost::asio::use_awaitable);
}

task<void> PlayerDBMySQL::Disconnect()
{
	co_await m_Conn.async_close(boost::asio::use_awaitable);
}

task<bool> PlayerDBMySQL::HasPlayer(const std::string_view& name)
{
	auto stmt = co_await m_Conn.async_prepare_statement("SELECT COUNT(*) FROM player WHERE name=?", boost::asio::use_awaitable);
	boost::mysql::results result;
	co_await m_Conn.async_execute(stmt.bind(name), result, boost::asio::use_awaitable);
	co_return result.rows().front().at(0).as_int64() > 0;
}

task<std::optional<PlayerData>> PlayerDBMySQL::GetPlayerByName(const std::string_view& name)
{
	auto stmt = co_await m_Conn.async_prepare_statement("SELECT id, email, password, country FROM player WHERE name=?", boost::asio::use_awaitable);
	boost::mysql::results result;
	co_await m_Conn.async_execute(stmt.bind(name), result, boost::asio::use_awaitable);
	if (!result.empty()) {
		const auto& front = result.rows().front();
		co_return PlayerData{
			front.at(0).as_uint64(),
			name,
			front.at(1).as_string(),
			front.at(2).as_string(),
			front.at(3).as_string()
		};
	}

	co_return std::nullopt;
}

task<std::optional<PlayerData>> PlayerDBMySQL::GetPlayerByPID(std::uint64_t pid)
{
	auto stmt = co_await m_Conn.async_prepare_statement("SELECT name, email, password, country FROM player WHERE id=?", boost::asio::use_awaitable);
	boost::mysql::results result;
	co_await m_Conn.async_execute(stmt.bind(pid), result, boost::asio::use_awaitable);
	if (!result.empty()) {
		const auto& front = result.rows().front();
		co_return PlayerData{
			pid,
			front.at(0).as_string(),
			front.at(1).as_string(),
			front.at(2).as_string(),
			front.at(3).as_string()
		};
	}

	co_return std::nullopt;
}

task<std::vector<PlayerData>> PlayerDBMySQL::GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password)
{
	auto players = std::vector<PlayerData>{};
	auto stmt = co_await m_Conn.async_prepare_statement("SELECT id, name, country FROM player WHERE email=? AND password=?", boost::asio::use_awaitable);
	boost::mysql::results result;
	co_await m_Conn.async_execute(stmt.bind(email, password), result, boost::asio::use_awaitable);
	for (const auto& player : result.rows())
		players.emplace_back(player.at(0).as_uint64(), player.at(1).as_string(), email, password, player.at(2).as_string());
	
	co_return players;
}

task<void> PlayerDBMySQL::CreatePlayer(PlayerData& player)
{
	auto stmt = co_await m_Conn.async_prepare_statement("INSERT INTO player (name, password, email, country, rank_id) VALUES (?, ?, ?, ?, 0)", boost::asio::use_awaitable);
	boost::mysql::results result;
	co_await m_Conn.async_execute(stmt.bind(player.name, player.password, player.email, player.country), result, boost::asio::use_awaitable);
	player.id = result.last_insert_id();
}

task<void> PlayerDBMySQL::UpdatePlayer(const PlayerData& player)
{
	auto stmt = co_await m_Conn.async_prepare_statement("UPDATE player SET password=?, email=?, country=? WHERE name=?", boost::asio::use_awaitable);
	boost::mysql::results result;
	co_await m_Conn.async_execute(stmt.bind(player.password, player.email, player.country, player.name), result, boost::asio::use_awaitable);
}