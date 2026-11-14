#pragma once
#include "asio.h"
#include <cstdint>
#include <string>
#include <span>
#include <optional>

namespace gamespy {
	class GameDB;
	class PlayerDB;

	class StatsClient {
		boost::asio::ip::tcp::socket m_Socket;
		GameDB& m_GameDB;
		PlayerDB& m_PlayerDB;
		std::optional<boost::asio::ip::tcp::endpoint> m_SnapshotEndpoint;
		std::string m_ReadBuffer;
		std::string m_ServerChallenge;
		std::int32_t m_SessionKey;

	public:
		StatsClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB, const std::optional<boost::asio::ip::tcp::endpoint>& snapshotEndpoint);
		~StatsClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<void> SendPacket(std::string message);
		boost::asio::awaitable<std::optional<std::string>> ReadPacket();
		boost::asio::awaitable<bool> Authenticate();
		boost::asio::awaitable<void> HandleSnapshot(const std::string_view& data);
	};
}
