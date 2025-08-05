#include "bf2.h"
#include <print>
using namespace gamespy;
using Send = GameData::GameKey::Send;
using Store = GameData::GameKey::Store;

const auto bf2Data = GameData{
	.name = "battlefield2",
	.secretKey = "hW6m9a",
	.keys = {
		{ "hostname" },
		{ "country" },
		{ "gamename" },
		{ "gamever" },
		{ "mapname" },
		{ "gametype" },
		{ "gamevariant" },
		{ "numplayers", Send::as_short },
		{ "maxplayers", Send::as_short },
		{ "gamemode" },
		{ "password", Send::as_byte },
		{ "timelimit", Send::as_short },
		{ "roundtime", Send::as_short },
		{ "hostport", Send::as_short },
		// GameSpy/qr2/qr2.c:63 (#define MAX_LOCAL_IP 5)
		{ "localip0" },
		{ "localip1" },
		{ "localip2" },
		{ "localip3" },
		{ "localip4" },
		{ "localport", Send::as_short },
		{ "natneg", Send::as_byte },
		{ "statechanged", Send::no_send, Store::no_store },
		// bf2 specific
		{ "bf2_dedicated", Send::as_byte },
		{ "bf2_ranked", Send::as_byte, Store::no_store },
		{ "bf2_anticheat", Send::as_byte },
		{ "bf2_os" },
		{ "bf2_autorec", Send::as_byte },
		{ "bf2_d_idx" },
		{ "bf2_d_dl" },
		{ "bf2_voip", Send::as_byte },
		{ "bf2_autobalanced", Send::as_byte },
		{ "bf2_friendlyfire", Send::as_byte },
		{ "bf2_tkmode" },
		{ "bf2_startdelay", Send::as_string, Store::as_real },
		{ "bf2_spawntime", Send::as_string, Store::as_real },
		{ "bf2_sponsortext" },
		{ "bf2_sponsorlogo_url" },
		{ "bf2_communitylogo_url" },
		{ "bf2_scorelimit", Send::as_short },
		{ "bf2_ticketratio", Send::as_string, Store::as_real },
		{ "bf2_teamratio", Send::as_string, Store::as_real },
		{ "bf2_team1" },
		{ "bf2_team2" },
		{ "bf2_bots", Send::as_byte },
		{ "bf2_pure", Send::as_byte },
		{ "bf2_mapsize", Send::as_short },
		{ "bf2_globalunlocks", Send::as_byte },
		{ "bf2_fps", Send::as_string, Store::as_real },
		{ "bf2_plasma", Send::as_byte, Store::no_store },
		{ "bf2_reservedslots", Send::as_short },
		{ "bf2_coopbotratio", Send::as_string, Store::as_real },
		{ "bf2_coopbotcount", Send::as_short },
		{ "bf2_coopbotdiff", Send::as_short },
		{ "bf2_novehicles", Send::as_byte },
	}
};

BF2::BF2(boost::asio::io_context& context, ConnectionParams params)
	: Game{ bf2Data }, m_SSL{ boost::asio::ssl::context::tls_client }, m_Conn{ context, m_SSL }, m_Params(std::move(params))
{

}

BF2::~BF2()
{

}

task<void> BF2::Connect()
{
	auto resolver = boost::asio::ip::tcp::resolver{ m_Conn.get_executor() };
	const auto& endpoints = co_await resolver.async_resolve(m_Params.hostname, std::to_string(m_Params.port), boost::asio::use_awaitable);

	auto handshakeParams = boost::mysql::handshake_params{ m_Params.username, m_Params.password, m_Params.database };
	handshakeParams.set_ssl(boost::mysql::ssl_mode::enable);
	std::println("[bf2] connecting to {}:{} db={}", m_Params.hostname, m_Params.port, m_Params.database);
	co_await m_Conn.async_connect(*endpoints, handshakeParams, boost::asio::use_awaitable);
}

task<void> BF2::Disconnect()
{
	co_await m_Conn.async_close(boost::asio::use_awaitable);
}

task<std::vector<Game::SavedServer>> BF2::GetServers(const std::string_view& query, const std::vector<std::string_view>& fields, std::size_t limit)
{	
	auto stmt = co_await m_Conn.async_prepare_statement(R"(
		SELECT
			server.ip as ip,
			server.queryport as port,
			stats_provider.plasma as plasma
		FROM server
		JOIN stats_provider ON stats_provider.id = server.provider_id
		LIMIT 1
	)", boost::asio::use_awaitable);

	boost::mysql::results result;
	co_await m_Conn.async_execute(stmt.bind(), result, boost::asio::use_awaitable);
	auto rankedServers = std::map<std::pair<std::string, std::uint16_t>, bool>{};
	for (const auto& row : result.rows()) {
		auto conn = std::make_pair(row.at(0).as_string(), static_cast<std::uint16_t>(row.at(1).as_uint64()));
		rankedServers.emplace(conn, !!row.at(2).as_uint64());
	}

	auto servers = co_await Game::GetServers(query, fields, limit);
	for (auto& server : servers) {
		auto conn = std::make_pair(server.public_ip, server.public_port);
		auto iter = rankedServers.find(conn);
		if (iter != rankedServers.end()) {
			server.data["bf2_ranked"] = "1";
			server.data["bf2_plasma"] = iter->second ? "1" : "0";
		}
	}

	co_return servers;
}
