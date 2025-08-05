#pragma once
#include "asio.h"
#include <cstdint>
#include <span>

namespace gamespy {
	class PlayerDB;

	class SearchClient {
		boost::asio::ip::tcp::socket m_Socket;
		PlayerDB& m_DB;

	public:
		SearchClient() = delete;
		SearchClient(SearchClient&& rhs) = default;
		SearchClient& operator=(SearchClient&& rhs) = default;

		SearchClient(boost::asio::ip::tcp::socket socket, PlayerDB& db);
		~SearchClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<void> HandleSearchNicks(const std::span<const char>& packet);
		boost::asio::awaitable<void> HandleProfileExists(const std::span<const char>& packet);

		boost::asio::awaitable<void> SendError(std::uint32_t errorCode, const std::string_view& message, bool fatal = false);
	};
}