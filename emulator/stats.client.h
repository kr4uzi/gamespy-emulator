#pragma once
#include "asio.h"
#include "gamedb.h"
#include <string>

namespace gamespy {
	struct TextPacket;
	class StatsClient {
		boost::asio::ip::tcp::socket m_Socket;

		GameDB& m_DB;
		std::string m_ServerChallenge;
		std::int32_t m_SessionKey;

		enum class STATES {
			INITIALIZING = 0,
			AUTHENTICATING = 1,
			AUTHENTICATED = 2,
			LOGGING_OUT
		} m_State = STATES::INITIALIZING;

	public:
		StatsClient() = delete;
		StatsClient(const StatsClient& rhs) = delete;
		StatsClient& operator=(const StatsClient& rhs) = delete;

		StatsClient(StatsClient&& rhs) = default;
		StatsClient& operator=(StatsClient&& rhs) = default;

		StatsClient(boost::asio::ip::tcp::socket socket, GameDB& db);
		~StatsClient();

		boost::asio::awaitable<void> Process();

	private:

		boost::asio::awaitable<void> SendChallenge();
	};
}