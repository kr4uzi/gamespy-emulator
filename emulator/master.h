#pragma once
#include "gamedb.h"
#include "asio.h"
#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gamespy {
	struct QRPacket;

	// query and reporting server:
	// - handles "available" requests (%s.available.gamespy.com)
	// - endpoint to register game servers (master.gamepsy.com)
	class MasterServer {
		static constexpr std::uint16_t PORT = 27900;

		boost::asio::ip::udp::socket m_Socket;
		boost::asio::steady_timer m_CleanupTimer;
		GameDB& m_DB;

		struct server {
			Clock::time_point last_update;
			std::string proof;
			std::array<std::uint8_t, 4> instance;
			std::string gamename;
			std::map<std::string, std::string> values;
		};

		std::map<boost::asio::ip::udp::endpoint, server> m_AwaitingValidation;
		std::map<boost::asio::ip::udp::endpoint, server> m_Validated;

	public:
		MasterServer(boost::asio::io_context& context, GameDB& db);
		~MasterServer();

		boost::asio::awaitable<void> AcceptConnections();

		boost::asio::awaitable<void> HandleAvailable(const boost::asio::ip::udp::endpoint& client, QRPacket& packet);
		boost::asio::awaitable<void> HandleHeartbeat(const boost::asio::ip::udp::endpoint& client, QRPacket& packet);
		boost::asio::awaitable<void> HandleKeepAlive(const boost::asio::ip::udp::endpoint& client, QRPacket& packet);
		boost::asio::awaitable<void> HandleChallenge(const boost::asio::ip::udp::endpoint& client, QRPacket& packet);

	private:
		void Cleanup(const boost::system::error_code& ec);
	};
}