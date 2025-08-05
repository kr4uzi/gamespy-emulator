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
			too_small,
			unexpected_end,
			unknown_type
		};

		// GameSpy/qr2/qr2.c:51
		enum class Type : std::uint8_t {
			query = 0x00,
			challenge = 0x01,
			echo = 0x02,
			echo_response = 0x05, // unknown why this is not following the order
			heartbeat = 0x03,
			adderror = 0x04,
			client_message = 0x06,
			client_message_ack = 0x07,
			keepalive = 0x08,
			prequery_ip_verify = 0x09
		};

		const Type type;
		const std::array<std::uint8_t, 4> instance;
		const std::span<const std::uint8_t> data;

		static std::expected<QRPacket, ParseError> Parse(const std::span<const std::uint8_t>& buffer);

	protected:
		/*QRPacket(Type type, decltype(instance) instance, decltype(data) data)
			: type{ type }, instance{ instance }, data{ data }
		{

		}*/
	};

	struct QRHeartbeatPacket : QRPacket
	{
		const std::map<std::string_view, std::string_view> serverData;
		const std::vector<std::string_view> playerKeys;
		const std::vector<std::vector<std::string_view>> playerValues;
		const std::vector<std::string_view> teamKeys;
		const std::vector<std::vector<std::string_view>> teamValues;

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