#include "bf2.h"
#include <print>
using namespace gamespy;
using Send = GameData::GameKey::Send;
using Store = GameData::GameKey::Store;

namespace key {
	// bf2 expects all keys as strings
	constexpr Game::KeyType text(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_string,
			.store = Store::as_text
		};
	}

	constexpr Game::KeyType real(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_string,
			.store = Store::as_real
		};
	}

	constexpr Game::KeyType shrt(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_string,
			.store = Store::as_integer
		};
	}

	constexpr Game::KeyType byte(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_string,
			.store = Store::as_integer
		};
	}
}

const auto bf2Data = GameData{
	.name = "battlefield2",
	.secretKey = "hW6m9a",
	.keys = {
		key::text("hostname"),
		key::text("country"),
		key::text("gamename"),
		key::text("gamever"),
		key::text("mapname"),
		key::text("gametype"),
		key::text("gamevariant"),
		key::shrt("numplayers"),
		key::shrt("maxplayers"),
		key::text("gamemode"),
		key::byte("password"),
		key::shrt("timelimit"),
		key::shrt("roundtime"),
		key::shrt("hostport"),
		// GameSpy/qr2/qr2.c:63 (#define MAX_LOCAL_IP 5)
		key::text("localip0"),
		key::text("localip1"),
		key::text("localip2"),
		key::text("localip3"),
		key::text("localip4"),
		key::shrt("localport"),
		key::byte("natneg"),
		key::byte("statechanged"),
		// bf2 specific
		key::byte("bf2_dedicated"),
		key::byte("bf2_ranked"),
		key::byte("bf2_anticheat"),
		key::text("bf2_os"),
		key::byte("bf2_autorec"),
		key::text("bf2_d_idx"),
		key::text("bf2_d_dl"),
		key::byte("bf2_voip"),
		key::byte("bf2_autobalanced"),
		key::byte("bf2_friendlyfire"),
		key::text("bf2_tkmode"),
		key::real("bf2_startdelay"),
		key::real("bf2_spawntime"),
		key::text("bf2_sponsortext"),
		key::text("bf2_sponsorlogo_url"),
		key::text("bf2_communitylogo_url"),
		key::shrt("bf2_scorelimit"),
		key::real("bf2_ticketratio"),
		key::real("bf2_teamratio"),
		key::text("bf2_team1"),
		key::text("bf2_team2"),
		key::byte("bf2_bots"),
		key::byte("bf2_pure"),
		key::shrt("bf2_mapsize"),
		key::byte("bf2_globalunlocks"),
		key::real("bf2_fps"),
		key::byte("bf2_plasma"),
		key::shrt("bf2_reservedslots"),
		key::real("bf2_coopbotratio"),
		key::shrt("bf2_coopbotcount"),
		key::real("bf2_coopbotdiff"),
		key::byte("bf2_novehicles")
	}
};

BF2::BF2(boost::asio::io_context& context)
	: Game{ bf2Data }, m_Conn{ context }
{

}

BF2::BF2(boost::asio::io_context& context, ConnectionParams params)
	: Game{ bf2Data }, m_Conn{ context }, m_Params(std::move(params))
{

}

BF2::~BF2()
{

}

task<void> BF2::Connect()
{
	co_await Game::Connect();

	if (!m_Params) {
		std::println("[bf2] running in passive mode (no mysql params configured)");
		co_return;
	}

	auto params = boost::mysql::connect_params{};
	params.server_address.emplace_host_and_port(m_Params->hostname, m_Params->port);
	params.username = m_Params->username;
	params.password = m_Params->password;
	params.database = m_Params->database;
	std::println("[bf2] connecting to {}:{} db={}", m_Params->hostname, m_Params->port, m_Params->database);
	co_await m_Conn.async_connect(params);
}

task<void> BF2::Disconnect()
{
	co_await m_Conn.async_close(boost::asio::use_awaitable);
}

task<void> BF2::AddOrUpdateServer(IncomingServer& server)
{
	// TODO: implement ranked + plasma verification here
	co_await Game::AddOrUpdateServer(server);
}

task<std::vector<Game::SavedServer>> BF2::GetServers(const std::string_view& query, const std::vector<std::string_view>& fields, std::size_t limit)
{
	auto servers = co_await Game::GetServers(query, fields, limit);
	if (!m_Params)
		co_return servers;

	auto stmt = co_await m_Conn.async_prepare_statement(R"(
		SELECT
			server.ip as ip,
			server.queryport as port,
			stats_provider.plasma as plasma
		FROM server
		JOIN stats_provider ON stats_provider.id = server.provider_id
		WHERE stats_provider.authorized = 1
	)", boost::asio::use_awaitable);

	boost::mysql::results result;
	co_await m_Conn.async_execute(stmt.bind(), result, boost::asio::use_awaitable);
	auto rankedServers = std::map<std::pair<std::string, std::uint16_t>, bool>{};
	for (const auto& row : result.rows()) {
		auto conn = std::make_pair(row.at(0).as_string(), static_cast<std::uint16_t>(row.at(1).as_uint64()));
		rankedServers.emplace(conn, !!row.at(2).as_int64());
	}

	for (auto& server : servers) {
		auto conn = std::make_pair(server.public_ip, server.public_port);
		auto iter = rankedServers.find(conn);
		if (iter != rankedServers.end()) {
			server.data["bf2_ranked"] = "1";
			server.data["bf2_plasma"] = iter->second ? "1" : "0";
		}
		else {
			server.data["bf2_ranked"] = "0";
			server.data["bf2_plasma"] = "0";
		}
	}

	co_return servers;
}
