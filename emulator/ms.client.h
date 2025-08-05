#pragma once
#include "asio.h"
#include "sapphire.h"
#include "game.h"
#include <cstdint>
#include <optional>
#include <expected>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include "gamedb.h"

namespace gamespy {
	struct ServerListRequest
	{
		enum class Options : std::uint8_t {
			send_fields_for_all = 1 << 0,
			no_server_list      = 1 << 1,
			push_updates        = 1 << 2,
			alternate_source_ip = 1 << 3,
			send_groups         = 1 << 5,
			no_list_cache       = 1 << 6,
			limit_result_count  = 1 << 7
		};

		std::uint8_t protocolVersion;
		std::uint8_t encodingVersion;

		std::uint32_t fromGameVersion;

		std::string_view fromGame;
		std::string_view toGame;

		std::string_view challenge;

		std::string_view serverFilter;
		std::vector<std::string_view> fieldList;

		Options options;

		std::optional<boost::asio::ip::address_v4> alternateSourceIP;
		std::optional<std::uint32_t> limitResultCount;

		static constexpr std::size_t client_challenge_length = 8;
		enum class ParseError {
			unknown_protocol_version,
			unknown_encoding_version,
			invalid_key,
			too_many_keys,
			invalid_alternate_ip,
			insufficient_length
		};

		using bytes = std::span<const std::uint8_t>;
		static std::expected<ServerListRequest, ParseError> Parse(const bytes& packet);
	};

	inline constexpr bool operator&(ServerListRequest::Options Lhs, ServerListRequest::Options Rhs) {
		return (std::to_underlying(Lhs) & std::to_underlying(Rhs)) > 0;
	}

	class BrowserClient {
		boost::asio::ip::tcp::socket m_Socket;
		GameDB& m_DB;
		std::optional<sapphire> m_Cypher;

	public:
		BrowserClient(BrowserClient&& rhs) = default;
		BrowserClient& operator=(BrowserClient&& rhs) = default;

		BrowserClient(boost::asio::ip::tcp::socket socket, GameDB &db);
		~BrowserClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<void> StartEncryption(const std::string_view& clientChallenge, const Game& game);

		boost::asio::awaitable<void> HandleServerListRequest(const std::span<const std::uint8_t>& bytes);
		std::vector<std::uint8_t> PrepareServerListHeader(const Game& game, const ServerListRequest& request);
		std::vector<std::uint8_t> PrepareServer(const Game& game, const Game::SavedServer& server, const ServerListRequest& request, bool usePopularValues = false);

	private:
		BrowserClient() = delete;
		BrowserClient(const BrowserClient& rhs) = delete;
		BrowserClient& operator=(const BrowserClient& rhs) = delete;
	};
}