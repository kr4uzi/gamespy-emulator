#pragma once
#ifndef _GAMESPY_SL_REQUEST_
#define _GAMESPY_SL_REQUEST_

#include "asio.h"
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace gamespy {
	struct ServerListRequest
	{
		enum class Options : std::uint8_t {
			SEND_FIELDS_FOR_ALL = 1 << 0,
			NO_SERVER_LIST      = 1 << 1,
			PUSH_UPDATES        = 1 << 2,
			ALTERNATE_SOURCE_IP = 1 << 3,
			SEND_GROUPS         = 1 << 5,
			NO_LIST_CACHE       = 1 << 6,
			LIMIT_RESULT_COUNT  = 1 << 7
		};

		std::uint8_t protocolVersion;
		std::uint8_t encodingVersion;

		std::uint32_t fromGameVersion;

		std::string fromGame;
		std::string toGame;

		std::string challenge;

		std::string serverFilter;
		std::vector<std::string> fieldList;

		Options options;

		std::optional<boost::asio::ip::address_v4> alternateSourceIP;
		std::optional<std::uint32_t> limitResultCount;

		static constexpr std::size_t CHALLENGE_LENGTH = 8;
		enum class ParseError {
			UNKNOWN_PROTOCOL_VERSION,
			UNKNOWN_ENCODING_VERSION,
			INVALID_KEY,
			TOO_MANY_KEYS,
			INVALID_ALTERNATE_IP,
			INSUFFICIENT_LENGTH
		};

		using bytes = std::span<const std::uint8_t>;
		static std::expected<ServerListRequest, ParseError> Parse(const bytes& packet);
	};

	inline constexpr bool operator&(ServerListRequest::Options Lhs, ServerListRequest::Options Rhs) {
		return (std::to_underlying(Lhs) & std::to_underlying(Rhs)) > 0;
	}
}
#endif