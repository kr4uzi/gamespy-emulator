#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <utility>
#include <string>
#include <expected>
#include <span>

namespace gamespy {
	struct QRPacket {
		static constexpr auto PACKETEND = "\x00\x00\x00\x00";

		enum class ParseError {
			TOO_SMALL,
			UNEXPECTED_END,
			UNKNOWN_TYPE
		};

		enum class Type : std::uint8_t {
			QUERY = 0x00,
			CHALLENGE = 0x01,
			ECHO = 0x02,
			ECHO_RESPONSE = 0x05, // unknown why this is not following the order
			HEARTBEAT = 0x03,
			ADDERROR = 0x04,
			CLIENT_MESSAGE = 0x06,
			CLIENT_MESSAGE_ACK = 0x07,
			KEEPALIVE = 0x08,
			PREQUERY_IP_VERIFY = 0x09
		};

		Type type;
		std::array<std::uint8_t, 4> instance;
		std::vector<std::pair<std::string, std::string>> values;

		static std::expected<QRPacket, ParseError> Parse(const std::span<const std::uint8_t>& buffer);
	};
}