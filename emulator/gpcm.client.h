#pragma once
#include "asio.h"
#include "playerdb.h"
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gamespy {
	class LoginClient {
		boost::asio::ip::tcp::socket m_Socket;
		boost::asio::steady_timer m_HeartbeatTimer;

		PlayerDB& m_PlayerDB;
		std::optional<PlayerData> m_PlayerData;
		std::string m_ServerChallenge;
		std::string m_LoginTicket;

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

		LoginClient(boost::asio::ip::tcp::socket socket, PlayerDB& playerDB);
		~LoginClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<void> KeepAliveClient();

		boost::asio::awaitable<void> SendChallenge();
		boost::asio::awaitable<void> HandleLogin(const std::span<const char>& packet);
		boost::asio::awaitable<void> HandleNewUser(const std::span<const char>& packet);
		boost::asio::awaitable<void> HandleGetProfile(const std::span<const char>& packet);
		boost::asio::awaitable<void> HandleUpdateProfile(const std::span<const char>& packet);
		boost::asio::awaitable<void> HandleLogout(const std::span<const char>& packet);

		boost::asio::awaitable<void> SendPlayerData(std::uint32_t requestId);
		boost::asio::awaitable<void> SendError(std::uint32_t requestId, std::uint32_t errorCode, const std::string_view& message, bool fatal = false);
	};
}