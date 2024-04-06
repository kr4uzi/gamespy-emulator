#pragma once
#include "asio.h"

namespace gamespy {
	namespace utils { struct TextPacket; }
	class Database;

	class SearchClient {
		boost::asio::ip::tcp::socket m_Socket;
		Database& m_DB;

	public:
		SearchClient() = delete;
		SearchClient(SearchClient&& rhs) = default;
		SearchClient& operator=(SearchClient&& rhs) = default;

		SearchClient(boost::asio::ip::tcp::socket socket, Database& db);
		~SearchClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<void> HandleRetrieveProfiles(const utils::TextPacket& packet);
		boost::asio::awaitable<void> HandleProfileExists(const utils::TextPacket& packet);
	};
}