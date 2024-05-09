#pragma once
#include "asio.h"

namespace gamespy {
	struct TextPacket;
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
		boost::asio::awaitable<void> HandleRetrieveProfiles(const TextPacket& packet);
		boost::asio::awaitable<void> HandleProfileExists(const TextPacket& packet);
	};
}