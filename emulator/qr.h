#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <utility>
#include <string>
#include <map>
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

		const Type type;
		const std::array<std::uint8_t, 4> instance;
		const std::vector<std::uint8_t> data;

		static std::expected<QRPacket, ParseError> Parse(const std::span<const std::uint8_t>& buffer);

	protected:
		/*QRPacket(Type type, decltype(instance) instance, decltype(data) data)
			: type{ type }, instance{ instance }, data{ data }
		{

		}*/
	};

	struct QRHeartbeatPacket : QRPacket
	{
		const std::map<std::string, std::string> server;
		const std::vector<std::string> playerKeys;
		const std::vector<std::vector<std::string>> playerValues;
		const std::vector<std::string> teamKeys;
		const std::vector<std::vector<std::string>> teamValues;

		static std::expected<QRHeartbeatPacket, ParseError> Parse(const QRPacket& packet);

	private:
		using QRPacket::Parse;
		using QRPacket::data;

		/*QRHeartbeatPacket(Type type, decltype(instance) instance, decltype(server) server, decltype(playerKeys) playerKeys, decltype(playerValues) playerValues, decltype(teamKeys) teamKeys, decltype(teamValues) teamValues)
			: QRPacket{ type, instance, {} }, server{ server }, playerKeys{ playerKeys }, playerValues{ playerValues }, teamKeys{ teamKeys }, teamValues{ teamValues }
		{

		}*/
	};
}