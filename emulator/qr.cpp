#include "qr.h"
#include <print>
#include <ranges>
#include <utility>
#include <iterator>
using namespace gamespy;

std::expected<QRPacket, QRPacket::ParseError> QRPacket::Parse(const std::span<const std::uint8_t>& buffer)
{
	if (buffer.size() < 5)
		return std::unexpected(ParseError::too_small);

	if (buffer[0] > std::to_underlying(Type::prequery_ip_verify))
		return std::unexpected(ParseError::unknown_type);

	const auto data = buffer.subspan(5);
	if (!data.empty() && data.back() != 0)
		return std::unexpected(ParseError::unexpected_end);

	return QRPacket{
		.type = static_cast<Type>(buffer[0]),
		.instance = { buffer[1], buffer[2], buffer[3], buffer[4] },
		.data = data
	};
}

struct QRMap
{
	std::map<std::string_view, std::string_view> data;

	template<typename Iter = std::forward_iterator_tag>
	static std::expected<QRMap, QRPacket::ParseError> Parse(Iter& pos, const Iter& end)
	{
		using ParseError = QRPacket::ParseError;
		if (pos == end)
			return std::unexpected(ParseError::too_small);

		std::map<std::string_view, std::string_view> data;
		std::string_view key;
		while (pos != end) {
			auto strEnd = std::find(pos, end, '\0');
			if (strEnd == end)
				return std::unexpected(ParseError::too_small);

			auto str = std::string_view{ reinterpret_cast<const char*>(&*pos), reinterpret_cast<const char*>(&*strEnd) };
			pos = strEnd + 1;

			if (key.empty()) {
				if (str.empty()) {
					// empy string (indicates map is finished)
					break;
				}

				std::swap(key, str);
			}
			else {
				data.emplace(key, std::move(str));
				key = std::string_view{};
			}
		}

		return QRMap{
			.data = data
		};
	}
};

struct QRTable
{
	using row_t = std::vector<std::string_view>;
	row_t header;
	std::vector<row_t> rows;

	template<typename Iter = std::forward_iterator_tag>
	static std::expected<QRTable, QRPacket::ParseError> Parse(Iter& pos, const Iter& end, const std::string_view& headerEnd)
	{
		using ParseError = QRPacket::ParseError;
		if (pos == end)
			return std::unexpected(ParseError::too_small);

		std::uint16_t count = *pos++ << 8;
		if (pos == end)
			return std::unexpected(ParseError::too_small);
		count |= *pos++;
		
		bool headersParsed = false;
		row_t headers;
		std::vector<row_t> rows;

		while (pos != end) {
			auto columnEnd = std::find(pos, end, '\0');
			if (columnEnd == end)
				return std::unexpected(ParseError::too_small);

			auto column = std::string_view{ reinterpret_cast<const char*>(&*pos), reinterpret_cast<const char*>(&*columnEnd) };
			pos = columnEnd + 1;

			if (!headersParsed && column.ends_with(headerEnd))
				headers.push_back(std::move(column));
			else {
				if (headers.empty())
					return std::unexpected(ParseError::unexpected_end);

				// empty column indicates end of headers
				if (column.empty()) {
					headersParsed = true;
					if (count == 0)
						break;

					continue;
				}

				if (rows.empty() || rows.back().size() == headers.size())
					rows.emplace_back();

				rows.back().push_back(std::move(column));

				if (rows.size() == count && rows.back().size() == headers.size())
					break;
			}
		}

		if (rows.size() < count || (!rows.empty() && rows.back().size() != headers.size()))
			return std::unexpected(ParseError::too_small);

		return QRTable{
			.header = headers,
			.rows = rows
		};
	}
};

std::expected<QRHeartbeatPacket, QRPacket::ParseError> QRHeartbeatPacket::Parse(const QRPacket& packet)
{
	auto pos = packet.data.begin();
	auto end = packet.data.end();

	if (auto map = QRMap::Parse(pos, end); map) {
		if (auto players = QRTable::Parse(pos, end, "_"); players) {
			if (auto teams = QRTable::Parse(pos, end, "_t"); teams) {
				return QRHeartbeatPacket{
					packet.type,
					packet.instance,
					{},
					map->data,
					players->header,
					players->rows,
					teams->header,
					teams->rows
				};
			}
		}
		else if (end == packet.data.end()) {
			// heartbeat without players/teams is valid
			return QRHeartbeatPacket{
				packet.type,
				packet.instance,
				{},
				map->data
			};
		}
	}
	
	return std::unexpected(QRPacket::ParseError::too_small);
}

namespace {
#include <GameSpy/qr2/qr2.h>
	static_assert(sizeof(QRPacket::instance) == REQUEST_KEY_LEN, "REQUEST_KEY_LEN value missmatch");
}
