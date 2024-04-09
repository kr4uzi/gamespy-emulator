#include "database.h"
#include "master.h"
#include "utils.h"
#include "qr.h"
#include <numeric>
#include <print>
#include <span>
#include <string_view>
using namespace gamespy;
using boost::asio::ip::udp;

MasterServer::MasterServer(boost::asio::io_context& context, Database& db)
	: m_Socket(context, udp::endpoint(udp::v6(), PORT)), m_DB(db)
{
	std::println("[master] starting up: {} UDP", PORT);
	std::println("[master] (%s.available.gamespy.com)");
	std::println("[master] (master.gamepsy.com)");
	std::println("[master] (%s.master.gamepsy.com)");
}

MasterServer::~MasterServer()
{
	std::println("[master] shutting down");
}

std::vector<ServerData> gamespy::MasterServer::GetServers(const std::string_view& game, const std::string& filter) const noexcept
{
	return std::vector<ServerData>{
		ServerData{
			.public_ip = boost::asio::ip::make_address_v4("127.0.0.1"),
			.data{
				{"hostname", "TEST"},
				{"country", "TEST"},
				{"gamename", "battlefield2"},
				{"gamever", "1.5.3153-802.0"},
				{"mapname", "MapName"},
				{"gametype", "gpm_cq"},
				{"gamevariant", "pr"},
				{"numplayers", "100"},
				{"maxplayers", "100"},
				{"gamemode", "openplaying"},
				{"password", "0"},
				{"timelimit", "14400"},
				{"roundtime", "1"},
				{"hostport", "16567"},
				{"bf2_dedicated", "1"},
				{"bf2_ranked", "1"},
				{"bf2_anticheat", "0"},
				{"bf2_os", "win32"},
				{"bf2_autorec", "1"},
				{"bf2_d_idx", "http://"},
				{"bf2_d_dl", "http://"},
				{"bf2_voip", "1"},
				{"bf2_autobalanced", "0"},
				{"bf2_friendlyfire", "1"},
				{"bf2_tkmode", "No Punish"},
				{"bf2_startdelay", "240.0"},
				{"bf2_spawntime", "300.0"},
				{"bf2_sponsortext", "sponsortext!"},
				{"bf2_sponsorlogo_url", "http://"},
				{"bf2_communitylogo_url", "http://"},
				{"bf2_scorelimit", "100"},
				{"bf2_ticketratio", "100.0"},
				{"bf2_teamratio", "100.0"},
				{"bf2_team1", "US"},
				{"bf2_team2", "MEC"},
				{"bf2_bots", "0"},
				{"bf2_pure", "0"},
				{"bf2_mapsize", "64"},
				{"bf2_globalunlocks", "1"},
				{"bf2_fps", "35.0"},
				{"bf2_plasma", "1"},
				{"bf2_reservedslots", "16"},
				{"bf2_coopbotratio", "0"},
				{"bf2_coopbotcount", "0"},
				{"bf2_coopbotdiff", "0"},
				{"bf2_novehicles", "0"}
			}
		}
	};
}

boost::asio::awaitable<void> MasterServer::HandleAvailable(const udp::endpoint& client, QRPacket& packet)
{
	// this package is sent by clients and server to check if the gamespy endpoint is running
	// the response contains a 32-bit status flag which containts only 3 possible status: 0 = available, 1 = unavailable, 2 = temporarily unavailable
	// sample package: 0x09 0x00 0x00 0x00 0x00 0x62 0x61 0x74 0x74 0x6C 0x65 0x66 0x69 0x65 0x6C 0x64 0x32 0x00
	//                     |   INSTANCE KEY   |  b    a    t    t    l    e    f    i    e    l    d    2  |
	if (packet.values.size() == 1) {
		if (m_DB.HasGame(packet.values.front().first))
			co_await m_Socket.async_send_to(boost::asio::buffer("\xFE\xFD\x09\0\0\0\0"), client, boost::asio::use_awaitable);
		else
			co_await m_Socket.async_send_to(boost::asio::buffer("\xFE\xFD\x09\0\0\0\1"), client, boost::asio::use_awaitable);
	}
	else
		std::println("[master] received invalid IP VERIFY packet (too short)");
}

boost::asio::awaitable<void> MasterServer::HandleHeartbeat(const udp::endpoint& client, QRPacket& packet)
{
	// this package is sent by servers to create and upate the server data
	// a rudimentary (and insufficient) protection is added via a simple challenge-response-authentication:
	// upon first HEARTBEAT package, the master server sends a challenge to the gameserver
	// the gameserver will then use its "private" password to encrypt the whole packet-data and send it back to the masterserver
	// when both encryptions match, the gameserver is validated and can be seen by the clients
	//
	// sample packet:
	// 0x03 (4-byte instance key)(n-bytes gameserver values: gamename 0x00 battlefield2 0x00 gamever 0x00 1.5....0x00) 0x00

	std::map<std::string, std::string> properties;
	for (const auto& [key, value] : packet.values) {
		// player data delimiter
		if (key.starts_with('\2'))
			break;

		properties[key] = value;
	}

	const auto& gamename = properties["gamename"];
	if (!m_DB.HasGame(gamename)) {
		std::println("[master] received HEARTBEAT for unknown game ({})", gamename);
		co_return;
	}

	auto game = m_DB.GetGame(gamename);
	auto& servers = m_Servers[gamename];
	servers.reserve(1024);

	const auto addr = client.address();
	const auto port = client.port();

	auto serverIter = std::find_if(servers.begin(), servers.end(), [&](const auto& server) { return server.public_ip == addr && server.public_port == port; });
	if (serverIter == servers.end() && servers.size() >= servers.capacity())
		co_return; // cannot to store more servers!

	if (serverIter != servers.end()) {
		serverIter->data = properties;
		serverIter->last_update = std::time(nullptr);
	}
	else {
		// Note: The challenge needs to be even-sized so that the base64 encoding can be generated without padding
		// This is required because the gamespy encoding is base64-ish and handles the padding differently
		constexpr std::uint8_t backendOptions = 0;
		auto challenge = utils::random_string("ABCDEFGHJIKLMNOPQRSTUVWXYZ123456789", 7);
		auto responseData = std::format("{}{:2X}{:8X}{:4X}", challenge, backendOptions, addr.to_v4().to_uint(), port);

		auto& server = servers.emplace_back(ServerData{
			.last_update = std::time(nullptr),
			.proof = utils::encode(game.GetSecretKey(), responseData),
			.instance = packet.instance,
			.public_ip = addr,
			.public_port = port,
			.data = properties
		});

		std::vector<uint8_t> response;
		response.push_back(0xFE);
		response.push_back(0xFD);
		response.push_back(0x01);
		response.append_range(server.instance);
		response.append_range(responseData);
		response.push_back(0);

		co_await m_Socket.async_send_to(boost::asio::buffer(response), client, boost::asio::use_awaitable);
	}
}

boost::asio::awaitable<void> MasterServer::HandleKeepAlive(const udp::endpoint& client, QRPacket& packet)
{
	// example packet: 0x08 (4-byte-instance-id) 0x00

	const auto addr = client.address();
	const auto port = client.port();

	for (auto& games : m_Servers) {
		for (auto& server : games.second) {
			if (server.public_ip == addr && server.public_port == port) {
				server.last_update = std::time(nullptr);
				continue;
			}
		}
	}

	std::println("[master] received KEEPALIVE for unknown server {}:{}", addr.to_string(), port);
	co_return;
}

boost::asio::awaitable<void> MasterServer::HandleChallenge(const udp::endpoint& client, QRPacket& packet)
{
	if (packet.values.size() != 1) {
		std::println("[master] received invalid challenge packet (size={})", packet.values.size());
		co_return;
	}

	const auto addr = client.address();
	const auto port = client.port();
	const auto& challenge = packet.values.front().first;
	for (auto& games : m_Servers) {
		for (auto& server : games.second) {
			if (server.public_ip == addr && server.public_port == port && server.instance == packet.instance) {
				if (server.proof == challenge) {
					server.validated = true;

					std::vector<std::uint8_t> response;
					response.push_back(0xFE);
					response.push_back(0xFD);
					response.push_back(0x0A);
					response.append_range(server.instance);

					co_await m_Socket.async_send_to(boost::asio::buffer(response), client, boost::asio::use_awaitable);
				}
				else
					std::println("[master] server failed to validate");

				co_return;
			}
		}
	}

	std::println("[master] received challenge for an unknown server");
}

boost::asio::awaitable<void> MasterServer::AcceptConnections()
{
	std::array<std::uint8_t, 1400> buff;
	while (m_Socket.is_open()) {
		udp::endpoint client;
		auto [error, length] = co_await m_Socket.async_receive_from(boost::asio::buffer(buff), client, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error || length == 0)
			continue;
		
		auto packet = QRPacket::Parse(std::span<std::uint8_t>(buff.data(), length));
		if (!packet) {
			std::println("[master] failed to parse packet");
			continue;
		}

		using Type = QRPacket::Type;
		switch (packet->type)
		{
		case Type::PREQUERY_IP_VERIFY:
			co_await HandleAvailable(client, *packet);
			break;
		case Type::HEARTBEAT:
			co_await HandleHeartbeat(client, *packet);
			break;
		case Type::KEEPALIVE:
			co_await HandleKeepAlive(client, *packet);
			break;
		case Type::CHALLENGE:
			co_await HandleChallenge(client, *packet);
			break;
		default:
			std::println("[master] Unknown MSG {}", std::to_underlying(packet->type));
		}
	}
}
