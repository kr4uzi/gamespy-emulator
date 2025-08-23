#include "master.h"
#include "gamedb.h"
#include "utils.h"
#include "qr.h"
#include <numeric>
#include <print>
#include <span>
#include <string_view>
#include <boost/asio/experimental/awaitable_operators.hpp>

using namespace gamespy;
using boost::asio::ip::udp;

MasterServer::MasterServer(boost::asio::io_context& context, GameDB& db)
	: m_Socket{ context, udp::endpoint{ udp::v4(), PORT } }, m_CleanupTimer{ context }, m_DB { db }
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

boost::asio::awaitable<void> MasterServer::Cleanup()
{
	while (true) {
		m_CleanupTimer.expires_after(std::chrono::seconds{ 60 });
		const auto& [error] = co_await m_CleanupTimer.async_wait(boost::asio::as_tuple);
		if (error) break;

		const auto& now = Clock::now();
		for (auto& servers : std::array{ &m_AwaitingValidation, &m_Validated }) {
			for (auto i = servers->begin(); i != servers->end(); ) {
				auto timeSinceLastUpdate = std::chrono::duration_cast<std::chrono::seconds>(now - i->second.last_update);
				if (timeSinceLastUpdate > std::chrono::seconds{ 60 }) {
					std::println("[master][server][{}] {}:{} timed out", i->second.gamename, i->first.address().to_string(), i->first.port());
					auto game = co_await m_DB.GetGame(i->second.gamename);
					co_await game->RemoveServers({ std::make_pair(i->first.address().to_string(), i->first.port()) });
					i = servers->erase(i);
				}
				else
					++i;
			}
		}
	}
	
}

boost::asio::awaitable<void> MasterServer::HandleAvailable(const udp::endpoint& client, QRPacket& packet)
{
	// this package is sent by clients and server to check if the gamespy endpoint is running
	// the response contains a 32-bit status flag which containts only 3 possible status: 0 = available, 1 = unavailable, 2 = temporarily unavailable
	// sample package: 0x09 0x00 0x00 0x00 0x00 0x62 0x61 0x74 0x74 0x6C 0x65 0x66 0x69 0x65 0x6C 0x64 0x32 0x00
	//                     |   INSTANCE KEY   |  b    a    t    t    l    e    f    i    e    l    d    2  |
	auto name = std::string_view{ reinterpret_cast<const char*>(packet.data.data()), packet.data.size() - 1 };
	if (co_await m_DB.HasGame(name)) {
		auto game = co_await m_DB.GetGame(name);
		auto available = game->availability();
		using Availability = decltype(available);
		switch (available) {
		case Availability::available:
			co_await m_Socket.async_send_to(boost::asio::buffer("\xFE\xFD\x09\0\0\0\0"), client, boost::asio::use_awaitable);
			break;
		case Availability::disabled_temporary:
			co_await m_Socket.async_send_to(boost::asio::buffer("\xFE\xFD\x09\0\0\0\1"), client, boost::asio::use_awaitable);
			break;
		case Availability::disabled_permanently:
			co_await m_Socket.async_send_to(boost::asio::buffer("\xFE\xFD\x09\0\0\0\2"), client, boost::asio::use_awaitable);
			break;
		}
	}
}

boost::asio::awaitable<void> MasterServer::HandleHeartbeat(const udp::endpoint& client, QRPacket& _packet)
{
	const auto packet = QRHeartbeatPacket::Parse(_packet);
	if (!packet) {
		std::println("[master] received invalid HEARTBEAT packet");
		co_return;
	}

	// this package is sent by servers to create and upate the server data
	// a rudimentary (and insufficient) protection is added via a simple challenge-response-authentication:
	// upon first HEARTBEAT package, the master server sends a challenge to the gameserver
	// the gameserver will then use its "private" password to encrypt the whole packet-data and send it back to the masterserver
	// when both encryptions match, the gameserver is validated and can be seen by the clients
	//
	// sample packet:
	// 0x03 (4-byte instance key)(n-bytes gameserver values: gamename 0x00 battlefield2 0x00 gamever 0x00 1.5....0x00) 0x00
	if (!packet->serverData.contains("gamename")) {
		std::println("[master] received HEARTBEAT with empty gamename");
		co_return;
	}

	const auto& gamename = packet->serverData.at("gamename");
	if (!co_await m_DB.HasGame(gamename)) {
		std::println("[master] received HEARTBEAT for unknown game {}", gamename);
		co_return;
	}

	auto game = co_await m_DB.GetGame(gamename);
	if (m_Validated.contains(client)) {
		auto addr = client.address().to_string();
		auto server = Game::IncomingServer{
			.last_update = Clock::now(),
			.public_ip = addr,
			.public_port = client.port(),
			.data = packet->serverData
		};
		co_await game->AddOrUpdateServer(server);
	}
	else if (!m_AwaitingValidation.contains(client)) {
		// Note: The challenge needs to be even-sized so that the base64 encoding can be generated without padding
		// This is required because the gamespy encoding is only base64-ish and handles the padding differently than regular base64 encoding
		
		// Note: query challenge is used to prevent id-spoofing but this isn't yet implemented
		//enum BACKEND_OPTIONS : std::uint8_t {
		//	QR2_USE_QUERY_CHALLENGE = 128
		//} backendOptions{ 0 };
		
		auto challenge = utils::random_string("ABCDEFGHJIKLMNOPQRSTUVWXYZ123456789", 7);
		auto responseData = std::format("{}{:2X}{:8X}{:4X}", challenge, std::to_underlying(game->backend()), client.address().to_v4().to_uint(), client.port());

		std::vector<uint8_t> response;
		response.push_back(0xFE);
		response.push_back(0xFD);
		response.push_back(0x01);
		response.append_range(packet->instance);
		response.append_range(responseData);
		response.push_back(0);

		m_AwaitingValidation.emplace(client, server{ 
			.last_update = Clock::now(),
			.proof = utils::encode(game->secretKey(), responseData),
			.instance = packet->instance,
			.gamename = std::string{ gamename },
			.values = std::map<std::string, std::string>(packet->serverData.begin(), packet->serverData.end())
		});
		co_await m_Socket.async_send_to(boost::asio::buffer(response), client, boost::asio::use_awaitable);
	}
	else if (m_AwaitingValidation.contains(client)) {
		auto& server = m_AwaitingValidation.at(client);
		server.last_update = Clock::now();
		server.values = std::map<std::string, std::string>(packet->serverData.begin(), packet->serverData.end());
	}
}

boost::asio::awaitable<void> MasterServer::HandleKeepAlive(const udp::endpoint& client, QRPacket& packet)
{
	// example packet: 0x08 (4-byte-instance-id) 0x00

	if (m_AwaitingValidation.contains(client))
		m_AwaitingValidation[client].last_update = Clock::now();
	else if (m_Validated.contains(client))
		m_Validated[client].last_update = Clock::now();
	else
		std::println("[master] received KEEPALIVE for unknown server {}:{}", client.address().to_string(), client.port());

	co_return;
}

boost::asio::awaitable<void> MasterServer::HandleChallenge(const udp::endpoint& client, QRPacket& packet)
{
	auto iter = m_AwaitingValidation.find(client);
	if (iter != m_AwaitingValidation.end()) {
		const auto& challenge = std::string{ reinterpret_cast<const char*>(packet.data.data()) };
		if (iter->second.proof == challenge) {
			// Note: instance is currently ignored
			m_Validated.emplace(client, iter->second);

			std::vector<std::uint8_t> response;
			response.push_back(0xFE);
			response.push_back(0xFD);
			response.push_back(0x0A);
			response.append_range(iter->second.instance);

			co_await m_Socket.async_send_to(boost::asio::buffer(response), client, boost::asio::use_awaitable);

			auto addr = client.address().to_string();
			auto server = Game::IncomingServer{
				.last_update = iter->second.last_update,
				.public_ip = addr,
				.public_port = client.port(),
				.data = std::map<std::string_view, std::string_view>{ std::from_range, iter->second.values }
			};

			auto game = co_await m_DB.GetGame(iter->second.gamename);
			co_await game->AddOrUpdateServer(server);
			std::println("[master][server][{}] {}:{} added", iter->second.gamename, server.public_ip, server.public_port);
		}

		m_AwaitingValidation.erase(iter);
	}
	else
		std::println("[master] received challenge for an unknown server");
}

boost::asio::awaitable<void> MasterServer::Run()
{
	using namespace boost::asio::experimental::awaitable_operators;
	co_await (AcceptConnections() && Cleanup());
}

boost::asio::awaitable<void> MasterServer::AcceptConnections()
{
	std::array<std::uint8_t, 1400> buff;
	while (m_Socket.is_open()) {
		udp::endpoint client;
		const auto& [error, length] = co_await m_Socket.async_receive_from(boost::asio::buffer(buff), client, boost::asio::as_tuple);
		if (error || length == 0) break;

		try {
			auto packet = QRPacket::Parse(std::span{ buff.data(), length });
			if (!packet) {
				std::println("[master] failed to parse packet");
				continue;
			}

			using Type = QRPacket::Type;
			switch (packet->type)
			{
			case Type::prequery_ip_verify:
				co_await HandleAvailable(client, *packet);
				break;
			case Type::heartbeat:
				co_await HandleHeartbeat(client, *packet);
				break;
			case Type::keepalive:
				co_await HandleKeepAlive(client, *packet);
				break;
			case Type::challenge:
				co_await HandleChallenge(client, *packet);
				break;
			default:
				std::println("[master] Unknown MSG {}", std::to_underlying(packet->type));
			}
		}
		catch (std::exception& ex) {
			std::println("[master] exception: {}", ex.what());
		}
	}
}
