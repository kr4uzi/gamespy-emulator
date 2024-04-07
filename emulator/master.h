#pragma once
#include "asio.h"
#include <array>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gamespy {
	class Database;
	struct QRPacket;
	struct ServerData {
		std::time_t last_update;
		std::string proof;
		std::array<std::uint8_t, 4> instance;

		boost::asio::ip::address public_ip;
		std::optional<boost::asio::ip::port_type> public_port;
		std::optional<boost::asio::ip::address> private_ip;
		std::optional<boost::asio::ip::port_type> private_port;
		std::optional<boost::asio::ip::address> icmp_ip;
		std::map<std::string, std::string> data;
		std::string stats; // gamespy calls those "RULES"

		bool validated = false;
	};

	// query and reporting server:
	// - handles "available" requests (%s.available.gamespy.com)
	// - endpoint to register game servers (master.gamepsy.com)
	class MasterServer {
		static constexpr std::uint16_t PORT = 27900;

		boost::asio::ip::udp::socket m_Socket;
		Database& m_DB;
		std::map<std::string, std::vector<ServerData>> m_Servers;

	public:
		MasterServer(boost::asio::io_context& context, Database& db);
		~MasterServer();

		boost::asio::awaitable<void> AcceptConnections();

		std::vector<ServerData> GetServers(const std::string_view& game, const std::string& filter) const noexcept;

		boost::asio::awaitable<void> HandleAvailable(const boost::asio::ip::udp::endpoint& client, QRPacket& packet);
		boost::asio::awaitable<void> HandleHeartbeat(const boost::asio::ip::udp::endpoint& client, QRPacket& packet);
		boost::asio::awaitable<void> HandleKeepAlive(const boost::asio::ip::udp::endpoint& client, QRPacket& packet);
		boost::asio::awaitable<void> HandleChallenge(const boost::asio::ip::udp::endpoint& client, QRPacket& packet);
	};
}