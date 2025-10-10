#include "bf2.h"
#include <print>
#include <algorithm>
using namespace gamespy;
using Send = GameData::GameKey::Send;
using Store = GameData::GameKey::Store;

namespace key {
	// gamespy stores optimized values as strings:
	// GameSpy/serverbrowsing/sb_serverlist.c:985 (ParseServer)
	// GameSpy/serverbrowsing/sb_server.c:102 (SBServerAddIntKeyValue)
	constexpr Game::KeyType text(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_string,
			.store = Store::as_text,
			.type = "string"
		};
	}

	constexpr Game::KeyType real(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_string,
			.store = Store::as_real,
			.type = "float"
		};
	}

	constexpr Game::KeyType shrt(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_short,
			.store = Store::as_integer,
			.type = "integer"
		};
	}

	constexpr Game::KeyType boolean(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_byte,
			.store = Store::as_integer,
			.type = "boolean"
		};
	}

	constexpr Game::KeyType ipaddr(const char* name)
	{
		return {
			.name = name,
			.send = Send::as_string,
			.store = Store::as_text,
			.type = "ip"
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
		key::boolean("password"),
		key::shrt("timelimit"),
		key::shrt("roundtime"),
		key::shrt("hostport"),
		// GameSpy/qr2/qr2.c:63 (#define MAX_LOCAL_IP 5)
		key::ipaddr("localip0"),
		key::ipaddr("localip1"),
		key::ipaddr("localip2"),
		key::ipaddr("localip3"),
		key::ipaddr("localip4"),
		key::shrt("localport"),
		key::boolean("natneg"),
		key::boolean("statechanged"),
		// bf2 specific
		key::boolean("bf2_dedicated"),
		key::boolean("bf2_ranked"),
		key::boolean("bf2_anticheat"),
		key::text("bf2_os"),
		key::boolean("bf2_autorec"),
		key::text("bf2_d_idx"),
		key::text("bf2_d_dl"),
		key::boolean("bf2_voip"),
		key::boolean("bf2_autobalanced"),
		key::boolean("bf2_friendlyfire"),
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
		key::boolean("bf2_bots"),
		key::boolean("bf2_pure"),
		key::shrt("bf2_mapsize"),
		key::boolean("bf2_globalunlocks"),
		key::real("bf2_fps"),
		key::boolean("bf2_plasma"),
		key::shrt("bf2_reservedslots"),
		key::real("bf2_coopbotratio"),
		key::shrt("bf2_coopbotcount"),
		key::real("bf2_coopbotdiff"),
		key::boolean("bf2_novehicles")
	}
};

BF2::BF2(boost::asio::io_context& context)
	: Game{ bf2Data }, m_Conn{ context }
{

}

BF2::BF2(boost::asio::io_context& context, boost::mysql::connect_params params)
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

	std::println("[bf2] connecting to {}:{} db={}", std::string(m_Params->server_address.hostname()), m_Params->server_address.port(), m_Params->database);
	co_await m_Conn.async_connect(*m_Params);
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

task<std::vector<Game::SavedServer>> BF2::GetServers(const std::string_view& _query, const std::vector<std::string_view>& fields, std::size_t limit, std::size_t skip)
{
	std::string_view query = _query;
	std::string copy;

	// remove all filters, "update server list", check any settings, "update server list"
	// => the gametype selection will be concatenated to rest of the query, e.g. bf2_ranked = 1gametype like '%gpcm_cq%'
	if (auto pos = query.find("gametype"); pos != std::string_view::npos && pos > 0 && query[pos - 1] != ' ') {
		copy.assign(query);
		copy.insert(pos, " and ");
		query = copy;
	}

	// fix unescaped single quotes in hostname queries
	auto hostnameQuery = std::string_view{ "hostname like '%" };
	if (auto pos = query.find(hostnameQuery); pos != std::string_view::npos) {
		pos += hostnameQuery.size();
		auto end = query.find("%'", pos);
		if (end != std::string_view::npos) {
			copy.assign(query);
			for (; pos < end; pos++) {
				if (copy[pos] == '\'') {
					copy.insert(pos, "'");
					pos++; // skip the character we just inserted
					end++;
				}
			}
			query = copy;
		}
	}

	auto servers = co_await Game::GetServers(query, fields, limit, skip);
	if (!m_Params || servers.size() == 0)
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
