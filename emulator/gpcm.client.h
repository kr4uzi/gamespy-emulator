#pragma once
#include "asio.h"
#include "playerdb.h"
#include <optional>
#include <string>

namespace gamespy {
	class GameDB;
	struct TextPacket;
	class LoginClient {
		boost::asio::ip::tcp::socket m_Socket;
		boost::asio::deadline_timer m_HeartBeatTimer;

		GameDB& m_GameDB;
		PlayerDB& m_PlayerDB;
		std::optional<PlayerData> m_PlayerData;
		std::string m_ServerChallenge;
		bool m_ProfileDataSent = false;

		enum class STATES {
			INITIALIZING = 0,
			AUTHENTICATING = 1,
			AUTHENTICATED = 2,
			LOGGING_OUT
		} m_State = STATES::INITIALIZING;

	public:
		LoginClient() = delete;
		LoginClient(const LoginClient& rhs) = delete;
		LoginClient& operator=(const LoginClient& rhs) = delete;

		LoginClient(LoginClient&& rhs) = default;
		LoginClient& operator=(LoginClient&& rhs) = default;

		LoginClient(boost::asio::ip::tcp::socket socket, GameDB& gameDB, PlayerDB& playerDB);
		~LoginClient();

		boost::asio::awaitable<void> Process();

	private:
		std::string GeneratePlayerData() const;
		boost::asio::awaitable<void> KeepAliveClient();

		boost::asio::awaitable<void> SendChallenge();
		boost::asio::awaitable<void> HandleLogin(const TextPacket& packet);
		boost::asio::awaitable<void> HandleNewUser(const TextPacket& packet);
		boost::asio::awaitable<void> HandleGetProfile(const TextPacket& packet);
		boost::asio::awaitable<void> HandleUpdateProfile(const TextPacket& packet);
		boost::asio::awaitable<void> HandleLogout(const TextPacket& packet);
	};
}