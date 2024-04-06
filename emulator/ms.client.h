#pragma once
#include "asio.h"
#include "sapphire.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gamespy {
	class Database;
	class GameData;
	struct ServerData;
	class MasterServer;
	struct ServerListRequest;

	class BrowserClient {
		boost::asio::ip::tcp::socket m_Socket;
		MasterServer& m_Master;
		Database& m_DB;
		std::optional<sapphire> m_Cypher;

	public:
		BrowserClient(BrowserClient&& rhs) = default;
		BrowserClient& operator=(BrowserClient&& rhs) = default;

		BrowserClient(boost::asio::ip::tcp::socket socket, MasterServer& master, Database& db);
		~BrowserClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<void> StartEncryption(const std::string_view& clientChallenge, const GameData& game);

		boost::asio::awaitable<void> HandleServerListRequest(const std::span<const std::uint8_t>& bytes);
		std::vector<std::uint8_t> PrepareServerListHeader(const GameData& game, const ServerListRequest& request);
		std::vector<std::uint8_t> PrepareServer(const GameData& game, const ServerData& server, const ServerListRequest& request, bool usePopularValues = false);

	private:
		BrowserClient() = delete;
		BrowserClient(const BrowserClient& rhs) = delete;
		BrowserClient& operator=(const BrowserClient& rhs) = delete;
	};
}