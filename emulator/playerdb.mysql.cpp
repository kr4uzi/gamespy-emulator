#include "playerdb.mysql.h"
#include <print>
using namespace gamespy;

PlayerDBMySQL::PlayerDBMySQL(boost::asio::io_context& context, decltype(m_Params) params)
	: m_Pool{ context, mysql_pool_params(params) }, m_Params{ std::move(params) }
{

}

PlayerDBMySQL::~PlayerDBMySQL()
{

}

task<void> PlayerDBMySQL::Connect()
{
	std::println("[playerdb] connecting to {}:{} db={}", std::string(m_Params.server_address.hostname()), m_Params.server_address.port(), m_Params.database);
	m_Pool.async_run(boost::asio::detached);
	auto connection = co_await mysql_connection(m_Pool);
}

task<void> PlayerDBMySQL::Disconnect()
{
	m_Pool.cancel();
	co_return;
}

task<bool> PlayerDBMySQL::HasPlayer(const std::string_view& name)
{
	auto connection = co_await mysql_connection(m_Pool);
	boost::mysql::results result;
	co_await connection->async_execute(boost::mysql::with_params("SELECT COUNT(*) FROM player WHERE name={}", name), result, boost::asio::use_awaitable);
	co_return mysql_unsigned(result.rows().front().at(0)) > 0;
}

task<std::optional<PlayerData>> PlayerDBMySQL::GetPlayerByName(const std::string_view& name)
{
	auto connection = co_await mysql_connection(m_Pool);
	boost::mysql::results result;
	co_await connection->async_execute(boost::mysql::with_params("SELECT id, email, password, country FROM player WHERE name={}", name), result, boost::asio::use_awaitable);
	if (!result.rows().empty()) {
		const auto& front = result.rows().front();
		co_return PlayerData{
			mysql_unsigned(front.at(0)),
			name,
			mysql_text(front.at(1)),
			mysql_text(front.at(2)),
			mysql_text(front.at(3))
		};
	}

	co_return std::nullopt;
}

task<std::optional<PlayerData>> PlayerDBMySQL::GetPlayerByPID(std::uint64_t pid)
{
	auto connection = co_await mysql_connection(m_Pool);
	boost::mysql::results result;
	co_await connection->async_execute(boost::mysql::with_params("SELECT name, email, password, country FROM player WHERE id={}", pid), result, boost::asio::use_awaitable);
	if (!result.rows().empty()) {
		const auto& front = result.rows().front();
		co_return PlayerData{
			pid,
			mysql_text(front.at(0)),
			mysql_text(front.at(1)),
			mysql_text(front.at(2)),
			mysql_text(front.at(3))
		};
	}

	co_return std::nullopt;
}

task<std::vector<PlayerData>> PlayerDBMySQL::GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password)
{
	auto players = std::vector<PlayerData>{};
	auto connection = co_await mysql_connection(m_Pool);
	boost::mysql::results result;
	co_await connection->async_execute(boost::mysql::with_params("SELECT id, name, country FROM player WHERE LOWER(email)=LOWER({}) AND password={}", email, password), result, boost::asio::use_awaitable);
	for (const auto& player : result.rows())
		players.emplace_back(mysql_unsigned(player.at(0)), mysql_text(player.at(1)), email, password, mysql_text(player.at(2)));
	
	co_return players;
}

task<void> PlayerDBMySQL::CreatePlayer(PlayerData& player)
{
	auto connection = co_await mysql_connection(m_Pool);
	boost::mysql::results result;
	co_await connection->async_execute(boost::mysql::with_params("INSERT INTO player (name, password, email, country, rank_id) VALUES ({}, {}, {}, {}, 0)", player.name, player.password, player.email, player.country), result, boost::asio::use_awaitable);
	player.id = result.last_insert_id();
}

task<void> PlayerDBMySQL::UpdatePlayer(const PlayerData& player)
{
	auto connection = co_await mysql_connection(m_Pool);
	boost::mysql::results result;
	co_await connection->async_execute(boost::mysql::with_params("UPDATE player SET password={}, email={}, country={} WHERE name={}", player.password, player.email, player.country, player.name), result, boost::asio::use_awaitable);
}
