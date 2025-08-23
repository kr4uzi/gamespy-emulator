#pragma once
#ifndef _GAMESPY_SB_REQUEST_H_
#define _GAMESPY_SB_REQUEST_H_

#include "asio.h"
#include "game.h"
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gamespy
{
	struct ServerBrowsingPacket
	{
		enum class Type : std::uint8_t {
			server_list_request = 0,
			server_info_request,
			send_message_request,
			keepalive_request,
			maploop_request,
			playersearch_request,
			min = server_list_request,
			max = playersearch_request
		};

		Type type;
		std::span<const std::uint8_t> data;
	};

	struct ServerListRequest
	{
		enum class Options : std::uint32_t {
			send_fields_for_all = 1 << 0,
			no_server_list      = 1 << 1,
			push_updates        = 1 << 2,
			alternate_source_ip = 1 << 3,
			send_groups         = 1 << 5,
			no_list_cache       = 1 << 6,
			limit_result_count  = 1 << 7,
			min = send_fields_for_all,
			max = limit_result_count
		};

		std::uint8_t protocolVersion;
		std::uint8_t encodingVersion;

		std::uint32_t fromGameVersion;

		std::string_view fromGame;
		std::string_view toGame;

		std::span<const std::uint8_t, 8> challenge;

		std::string_view serverFilter;
		std::vector<std::string_view> fieldList;

		Options options;

		std::optional<boost::asio::ip::address_v4> alternateSourceIP;
		std::optional<std::uint32_t> limitResultCount;

		enum class ParseError {
			unknown_protocol_version,
			unknown_encoding_version,
			invalid_key,
			too_many_keys,
			invalid_alternate_ip,
			insufficient_length
		};

		static std::expected<ServerListRequest, ParseError> Parse(const std::span<const std::uint8_t>& packet);

		std::vector<std::uint8_t> GetResponseHeaderBytes(const Game& forGame, const boost::asio::ip::address_v4& socketIP) const;

		static std::vector<std::uint8_t> GetServerBytes(const Game& game, const Game::IncomingServer& server, const std::vector<std::string_view>& fieldList, bool usePopularList);
		static std::vector<std::uint8_t> GetServerBytes(const Game& game, const Game::SavedServer& server, const std::vector<std::string_view>& fieldList, bool usePopularList);
	};

	inline constexpr bool operator&(ServerListRequest::Options Lhs, ServerListRequest::Options Rhs) {
		return (std::to_underlying(Lhs) & std::to_underlying(Rhs)) > 0;
	}
}

#endif
