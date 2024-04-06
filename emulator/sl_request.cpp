#include "sl_request.h"
#include <ranges>
using namespace gamespy;

std::string ExtractString(const ServerListRequest::bytes& packet, ServerListRequest::bytes::iterator& start)
{
	const auto pkgEnd = packet.end();
	const auto strEnd = std::find(start, pkgEnd, '\0');
	if (strEnd == pkgEnd)
		throw ServerListRequest::ParseError::INSUFFICIENT_LENGTH;

	return std::string{ start, strEnd };
}

std::uint32_t ExtractUInt32(const ServerListRequest::bytes& packet, ServerListRequest::bytes::iterator& start)
{
	if (std::distance(start, packet.end()) < 4)
		throw ServerListRequest::ParseError::INSUFFICIENT_LENGTH;

	return static_cast<std::uint32_t>((*start++ << 24) | (*start++ << 16) | (*start++ << 8) | *start++);
}

std::expected<ServerListRequest, ServerListRequest::ParseError> ServerListRequest::Parse(const ServerListRequest::bytes& packet) {
	constexpr std::size_t MIN_SIZE = 1 /* protocol */ + 1 /* encoding */ + 4 /* 4 bytes gameversion (integer) */ +
		2 /* gamename (min 1 byte + terminator) */ + 2 /* gamename (is repeated) */ +
		8 /* client challenge */ + 1 /* query (can be empty, but terminator) */ + 3 /* field list (\\-prefix + 1 byte + terminator) */ +
		4 /* 4 bytes options (integer) */;
	if (packet.size() < MIN_SIZE)
		return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

	auto packetIter = packet.begin();

	auto protocol = static_cast<std::uint8_t>(*packetIter++);
	if (protocol != 0x01)
		return std::unexpected(ParseError::UNKNOWN_PROTOCOL_VERSION);

	auto encoding = static_cast<std::uint8_t>(*packetIter++);
	if (encoding != 0x03)
		return std::unexpected(ParseError::UNKNOWN_ENCODING_VERSION);

	// yup, thats right: exceptions used for control flow but they make the code much more readable...
	try {
		const auto gameversion = ExtractUInt32(packet, packetIter);
		const auto fromGame = ExtractString(packet, packetIter);
		const auto toGame = ExtractString(packet, packetIter);

		// the query is always CHALLENGE_LENGTH (8) bytes long, *not* null terminated (!) and followed by the server-list-query
		// which is null-terminated, but might be empty
		auto challenge = ExtractString(packet, packetIter);
		if (challenge.length() < CHALLENGE_LENGTH)
			return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

		const auto query = challenge.substr(CHALLENGE_LENGTH);
		challenge.resize(CHALLENGE_LENGTH);

		auto fields = std::vector<std::string>{};
		{
			std::string fieldStr = ExtractString(packet, packetIter);
			if (!fieldStr.empty() && fieldStr.front() != '\\')
				return std::unexpected(ParseError::INVALID_KEY);

			auto fieldNames = fieldStr.substr(1) // fields always start with '\\' which we need to remove before the split so we don't get an empty string
				| std::views::split('\\')
				| std::views::transform([](const auto& range) { return std::string{ range.begin(), range.end() }; });

			fields.insert(fields.end(), std::make_move_iterator(fieldNames.begin()), std::make_move_iterator(fieldNames.end()));
			if (fields.size() >= 255)
				return std::unexpected(ParseError::TOO_MANY_KEYS);
		}

		const auto options = ExtractUInt32(packet, packetIter);

		auto alternateIP = std::optional<boost::asio::ip::address_v4>{};
		if (options & std::to_underlying(Options::ALTERNATE_SOURCE_IP))
			alternateIP.emplace(ExtractUInt32(packet, packetIter));

		auto limit = std::optional<std::uint32_t>{};
		if (options & std::to_underlying(Options::LIMIT_RESULT_COUNT))
			limit.emplace(ExtractUInt32(packet, packetIter));

		return ServerListRequest{
			.protocolVersion = protocol,
			.encodingVersion = encoding,
			.fromGameVersion = gameversion,
			.fromGame = fromGame,
			.toGame = toGame,
			.challenge = challenge,
			.serverFilter = query,
			.fieldList = std::move(fields),
			.alternateSourceIP = std::move(alternateIP),
			.limitResultCount = std::move(limit)
		};
	}
	catch (ParseError e) {
		return std::unexpected(e);
	}
}

#if 0
std::expected<ServerListRequest, ServerListRequest::ParseError> ServerListRequest::Parse(const bytes& packet) {
	constexpr std::size_t MIN_SIZE = 1 /* protocol */ + 1 /* encoding */ + 4 /* 4 bytes gameversion (integer) */ +
		2 /* gamename (min 1 byte + terminator) */ + 2 /* gamename (is repeated) */ +
		8 /* client challenge */ + 1 /* query (can be empty, but terminator) */ + 3 /* field list (\\-prefix + 1 byte + terminator) */ +
		4 /* 4 bytes options (integer) */;
	if (packet.size() < MIN_SIZE)
		return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

	auto packetIter = packet.begin();

	auto protocol = static_cast<std::uint8_t>(*packetIter++);
	if (protocol != 0x01)
		return std::unexpected(ParseError::UNKNOWN_PROTOCOL_VERSION);

	auto encoding = static_cast<std::uint8_t>(*packetIter++);
	if (encoding != 0x03)
		return std::unexpected(ParseError::UNKNOWN_ENCODING_VERSION);

	auto gameversion = static_cast<std::uint32_t>((*packetIter++ << 24) | (*packetIter++ << 16) | (*packetIter++ << 8) | *packetIter++);

	auto fromGameBegin = packetIter;
	auto fromGameEnd = std::find(fromGameBegin, packet.end(), '\0');
	if (fromGameEnd == packet.end() || fromGameBegin == fromGameEnd)
		return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

	auto toGameBegin = fromGameEnd + 1;
	auto toGameEnd = std::find(toGameBegin, packet.end(), '\0');
	if (toGameEnd == packet.end() || toGameBegin == toGameEnd)
		return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

	// client challenge is not null terminated!
	auto clientChallengeBegin = toGameEnd + 1;
	auto clientChallengeEnd = std::ranges::next(clientChallengeBegin, 8, packet.end());
	auto queryEnd = std::find(clientChallengeEnd, packet.end(), '\0');
	if (queryEnd == packet.end())
		return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

	auto queryBegin = clientChallengeEnd;
	// query must be at least a empty string (null terminated)

	// fields must be at least a empty string (null terminated)
	auto fieldsBegin = queryEnd + 1;
	auto fieldsEnd = std::find(fieldsBegin, packet.end(), '\0');
	if (fieldsEnd != packet.end()) {
		// fields might be empty, but must always start with backslash
		if (*fieldsBegin && *fieldsBegin != '\\')
			return std::unexpected(ParseError::INVALID_KEY);

		// move to first character of the first key
		fieldsBegin++;
	}

	packetIter = fieldsEnd + 1;
	if (std::distance(packetIter, packet.end()) < 4)
		return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

	auto options = static_cast<std::uint32_t>((*packetIter++ << 24) | (*packetIter++ << 16) | (*packetIter++ << 8) | *packetIter++);

	auto alternateIP = std::optional<boost::asio::ip::address_v4>{};
	if (options & std::to_underlying(Options::ALTERNATE_SOURCE_IP)) {
		if (std::distance(packetIter, packet.end()) < 4)
			return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

		alternateIP.emplace((*packetIter++ << 24) | (*packetIter++ << 16) | (*packetIter++ << 8) | *packetIter++);
	}

	auto limit = std::optional<std::uint32_t>{};
	if (options & std::to_underlying(Options::LIMIT_RESULT_COUNT)) {
		if (std::distance(packetIter, packet.end()) < 4) {
			return std::unexpected(ParseError::INSUFFICIENT_LENGTH);
		}

		limit.emplace((*packetIter++ << 24) | (*packetIter++ << 16) | (*packetIter++ << 8) | *packetIter);
	}

	if (std::count(fieldsBegin, fieldsEnd, '\\') >= 255)
		return std::unexpected(ParseError::TOO_MANY_KEYS);

	auto fields = std::ranges::subrange(fieldsBegin, fieldsEnd)
		| std::views::split('\\')
		| std::views::transform([](const auto& x) { return std::string(x.begin(), x.end()); });

	return ServerListRequest{
		.protocolVersion = protocol,
		.encodingVersion = encoding,
		.fromGameVersion = gameversion,
		.fromGame = { fromGameBegin, fromGameEnd },
		.toGame = { toGameBegin, toGameEnd },
		.challenge = { clientChallengeBegin, clientChallengeEnd },
		.serverFilter = { queryBegin, queryEnd },
		.fieldList = { std::make_move_iterator(fields.begin()), std::make_move_iterator(fields.end()) },
		.alternateSourceIP = alternateIP,
		.limitResultCount = limit
	};
}
#endif