#include "qr.h"
#include <algorithm>
#include <utility>
using namespace gamespy;

std::expected<QRPacket, QRPacket::ParseError> QRPacket::Parse(const std::span<const std::uint8_t>& buffer)
{
	if (buffer.size() < 5)
		return std::unexpected(ParseError::too_small);

	if (buffer[0] > std::to_underlying(Type::prequery_ip_verify))
		return std::unexpected(ParseError::unknown_type);

	const auto type = static_cast<Type>(buffer[0]);
	const auto data = buffer.subspan(5);
	if ((!data.empty() && data.back() != 0)
		|| (data.empty() && (type == Type::prequery_ip_verify || type == Type::challenge)))
		return std::unexpected(ParseError::unexpected_end);

	return QRPacket{
		.type = type,
		.instance = { buffer[1], buffer[2], buffer[3], buffer[4] },
		.data = data
	};
}

namespace {
	using ParseError = QRPacket::ParseError;

	std::expected<std::string_view, ParseError> ReadString(std::span<const std::uint8_t>& input)
	{
		const auto end = std::ranges::find(input, 0);
		if (end == input.end())
			return std::unexpected(ParseError::unexpected_end);

		const auto size = static_cast<std::size_t>(end - input.begin());
		const auto value = std::string_view{ reinterpret_cast<const char*>(input.data()), size };
		input = input.subspan(size + 1);
		return value;
	}

	struct Table {
		std::vector<std::string_view> keys;
		std::vector<std::vector<std::string_view>> rows;
	};

	std::expected<Table, ParseError> ReadTable(std::span<const std::uint8_t>& input)
	{
		if (input.size() < 2)
			return std::unexpected(ParseError::too_small);
		const auto count = (static_cast<std::uint16_t>(input[0]) << 8) | input[1];
		input = input.subspan(2);

		Table table;
		while (true) {
			auto key = ReadString(input);
			if (!key) return std::unexpected(key.error());
			if (key->empty()) break;
			table.keys.push_back(*key);
		}

		// Games may report a count without registering any player/team keys.
		if (table.keys.empty()) return table;
		// Every value needs at least its terminator. Check before allocating rows.
		if (static_cast<std::size_t>(count) * table.keys.size() > input.size())
			return std::unexpected(ParseError::too_small);

		for (int row = 0; row < count; ++row) {
			auto& values = table.rows.emplace_back();
			for (std::size_t column = 0; column < table.keys.size(); ++column) {
				auto value = ReadString(input);
				if (!value) return std::unexpected(value.error());
				values.push_back(*value);
			}
		}
		return table;
	}
}

std::expected<QRHeartbeatPacket, QRPacket::ParseError> QRHeartbeatPacket::Parse(const QRPacket& packet)
{
	auto input = packet.data;
	std::map<std::string_view, std::string_view> serverData;
	while (true) {
		auto key = ReadString(input);
		if (!key) return std::unexpected(key.error());
		if (key->empty()) break;
		auto value = ReadString(input);
		if (!value) return std::unexpected(value.error());
		serverData.emplace(*key, *value);
	}

	// A heartbeat can omit both tables, but a partially present table is invalid.
	if (input.empty())
		return QRHeartbeatPacket{ packet.type, packet.instance, {}, std::move(serverData) };

	auto players = ReadTable(input);
	if (!players) return std::unexpected(players.error());
	auto teams = ReadTable(input);
	if (!teams) return std::unexpected(teams.error());
	if (!input.empty())
		return std::unexpected(ParseError::unexpected_end);

	return QRHeartbeatPacket{
		packet.type, packet.instance, {}, std::move(serverData),
		std::move(players->keys), std::move(players->rows),
		std::move(teams->keys), std::move(teams->rows)
	};
}
