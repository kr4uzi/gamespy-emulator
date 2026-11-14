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
		const auto [error] = co_await m_CleanupTimer.async_wait(boost::asio::as_tuple);
		if (error) break;

		const auto now = Clock::now();
		const auto expired = [&](const auto& entry) {
			return now - entry.second.last_update > std::chrono::seconds{ 60 };
		};
		std::erase_if(m_AwaitingValidation, expired);

		// Own expired records before awaiting: packet handlers also change these maps.
		std::vector<std::pair<udp::endpoint, server>> removed;
		for (auto it = m_Validated.begin(); it != m_Validated.end();) {
			if (expired(*it)) {
				removed.emplace_back(it->first, std::move(it->second));
				it = m_Validated.erase(it);
			}
			else ++it;
		}
		for (const auto& [client, record] : removed) {
			const auto address = client.address().to_string();
			std::println("[master][server][{}] {}:{} timed out", record.gamename, address, client.port());
			auto game = co_await m_DB.GetGame(record.gamename);
			const std::vector<std::pair<std::string_view, std::uint16_t>> endpoints{ { address, client.port() } };
			co_await game->RemoveServers(endpoints);
		}
	}
}

boost::asio::awaitable<void> MasterServer::HandleAvailable(const udp::endpoint& client, QRPacket& packet)
{
	// QRPacket::Parse requires a terminated payload for availability requests.
	const auto name = std::string_view{ reinterpret_cast<const char*>(packet.data.data()), packet.data.size() - 1 };
	if (!co_await m_DB.HasGame(name)) co_return;
	const auto game = co_await m_DB.GetGame(name);
	const auto response = std::array<std::uint8_t, 7>{
		0xFE, 0xFD, 0x09, 0, 0, 0, std::to_underlying(game->availability())
	};
	co_await m_Socket.async_send_to(boost::asio::buffer(response), client, boost::asio::use_awaitable);
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
	// qr2.c send_heartbeat(..., 2) is the server's explicit shutdown notice.
	if (const auto changed = packet->serverData.find("statechanged");
		changed != packet->serverData.end() && changed->second == "2") {
		if (const auto waiting = m_AwaitingValidation.find(client); waiting != m_AwaitingValidation.end()
			&& waiting->second.instance == packet->instance && waiting->second.gamename == gamename)
			m_AwaitingValidation.erase(waiting);
		if (const auto known = m_Validated.find(client); known != m_Validated.end()
			&& known->second.instance == packet->instance && known->second.gamename == gamename) {
			m_Validated.erase(known);
			const auto address = client.address().to_string();
			const std::vector<std::pair<std::string_view, std::uint16_t>> endpoints{ { address, client.port() } };
			co_await game->RemoveServers(endpoints);
		}
		co_return;
	}
	if (auto known = m_Validated.find(client); known != m_Validated.end()
		&& (known->second.gamename != gamename || known->second.instance != packet->instance)) {
		// An endpoint reused by a restarted/different game must validate again.
		auto previous = m_Validated.extract(known);
		auto previousGame = co_await m_DB.GetGame(previous.mapped().gamename);
		const auto address = client.address().to_string();
		const std::vector<std::pair<std::string_view, std::uint16_t>> endpoints{ { address, client.port() } };
		co_await previousGame->RemoveServers(endpoints);
	}
	if (auto waiting = m_AwaitingValidation.find(client); waiting != m_AwaitingValidation.end()
		&& (waiting->second.gamename != gamename || waiting->second.instance != packet->instance))
		m_AwaitingValidation.erase(waiting);

	if (m_Validated.contains(client)) {
		m_Validated.at(client).last_update = Clock::now();
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
		if (m_AwaitingValidation.size() + m_Validated.size() >= MAX_SERVERS)
			co_return;
		// 21 bytes avoids padding in GameSpy's base64 variant.
		auto challenge = utils::random_string("ABCDEFGHJIKLMNOPQRSTUVWXYZ123456789", 7);
		auto responseData = std::format("{}{:02X}{:08X}{:04X}", challenge, std::to_underlying(game->backend()), client.address().to_v4().to_uint(), client.port());

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
	for (auto* records : { &m_AwaitingValidation, &m_Validated }) {
		const auto found = records->find(client);
		if (found != records->end() && found->second.instance == packet.instance) {
			found->second.last_update = Clock::now();
			co_return;
		}
	}
}

boost::asio::awaitable<void> MasterServer::HandleChallenge(const udp::endpoint& client, QRPacket& packet)
{
	// Extract the record so cleanup cannot invalidate it while a send suspends.
	auto pending = m_AwaitingValidation.extract(client);
	if (pending.empty()) co_return;
	auto& record = pending.mapped();
	const auto challenge = std::string_view{ reinterpret_cast<const char*>(packet.data.data()), packet.data.size() - 1 };
	if (record.proof != challenge || record.instance != packet.instance) co_return;

	record.last_update = Clock::now();
	const auto address = client.address().to_string();
	auto incoming = Game::IncomingServer{
		.last_update = record.last_update,
		.public_ip = address,
		.public_port = client.port(),
		.data = { record.values.begin(), record.values.end() }
	};
	auto game = co_await m_DB.GetGame(record.gamename);
	co_await game->AddOrUpdateServer(incoming);
	m_Validated.insert_or_assign(client, record);

	std::vector<std::uint8_t> response{ 0xFE, 0xFD, 0x0A };
	response.append_range(record.instance);
	co_await m_Socket.async_send_to(boost::asio::buffer(response), client, boost::asio::use_awaitable);
	std::println("[master][server][{}] {}:{} added", record.gamename, address, client.port());
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
		if (error == boost::asio::error::message_size) continue;
		if (error == boost::asio::error::operation_aborted) co_return;
		if (error) throw boost::system::system_error(error);
		if (length == 0) continue;

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
