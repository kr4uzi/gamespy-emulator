#include "sb_request.h"
#include "game.h"
#include <array>
#include <bit>
#include <utility>
#include <limits>
#include <charconv>
using namespace gamespy;

namespace {
	constexpr std::uint8_t list_protocol_version = 1;
	constexpr std::uint8_t list_encoding_version = 3;
	constexpr std::size_t fields_total_length = 256;
	constexpr std::size_t filter_total_length = 256;

	// Note: Only malformed packets (invalid length in the header) will throw exceptions
	using PacketIterator = std::span<const std::uint8_t>::iterator;
	std::expected<std::string_view, ServerListRequest::ParseError> ExtractString(PacketIterator& start, const PacketIterator& end, std::string_view::size_type maxLength = std::string_view::npos)
	{
		const auto strEnd = std::find(start, end, '\0');
		if (strEnd == end)
			return std::unexpected(ServerListRequest::ParseError::insufficient_length);

		if (std::cmp_greater(std::distance(start, strEnd), maxLength))
			return std::unexpected(ServerListRequest::ParseError::too_many_keys);

		auto res = std::string_view{ reinterpret_cast<const char*>(&*start), reinterpret_cast<const char*>(&*strEnd) };
		start = strEnd + 1;
		return res;
	}

	template<std::size_t N>
	std::expected<std::span<const std::uint8_t, N>, ServerListRequest::ParseError> ExtractSpan(PacketIterator& start, const PacketIterator& end)
	{
		if (std::distance(start, end) < N)
			return std::unexpected(ServerListRequest::ParseError::insufficient_length);

		auto res = std::span<const std::uint8_t, N>(start, start + N);
		start += N;
		return res;
	}

	std::expected<std::uint32_t, ServerListRequest::ParseError> ExtractUInt32(PacketIterator& start, const PacketIterator& end)
	{
		if (std::distance(start, end) < 4)
			return std::unexpected(ServerListRequest::ParseError::insufficient_length);

		std::uint32_t value = 0;
		for (int byte = 0; byte < 4; ++byte)
			value = (value << 8) | *start++;
		return value;
	}

	// SBServerListConnectAndQuery uses little endian for version/result count,
	// but network byte order for options and alternate IP (sb_serverlist.c).
	auto ExtractUInt32LE(PacketIterator& start, const PacketIterator& end)
	{
		return ExtractUInt32(start, end).transform([](auto value) { return std::byteswap(value); });
	}

	auto ExtractOptions(PacketIterator& start, const PacketIterator& end)
	{
		return ExtractUInt32(start, end).and_then([](auto value) -> std::expected<ServerListRequest::Options, ServerListRequest::ParseError> {
			// Options are a bitmask; combinations can exceed the highest single flag.
			constexpr std::uint32_t knownOptions = 1 | 2 | 4 | 8 | 32 | 64 | 128;
			if ((value & ~knownOptions) == 0)
				return static_cast<ServerListRequest::Options>(value);

			return std::unexpected(ServerListRequest::ParseError::invalid_key);
		});
	}

	auto ExtractFieldList(PacketIterator& start, const PacketIterator& end)
	{
		return ExtractString(start, end, ::fields_total_length).and_then([](auto fieldStr) -> std::expected<std::vector<std::string_view>, ServerListRequest::ParseError> {
			if (fieldStr.empty()) return std::vector<std::string_view>{};
			if (fieldStr.front() != '\\')
				return std::unexpected(ServerListRequest::ParseError::invalid_key);

			auto fields = std::vector<std::string_view>{};
			auto fieldNames = fieldStr.substr(1) // fields always start with '\\' which we need to remove before the split so we don't get an empty string
				| std::views::split('\\')
				| std::views::transform([](const auto& range) { return std::string_view{ range.begin(), range.end() }; });

			for (auto field : fieldNames) {
				if (!Game::IsValidParamName(field))
					return std::unexpected(ServerListRequest::ParseError::invalid_key);
				fields.push_back(field);
			}
			return fields;
		});
	}

	std::expected<std::optional<boost::asio::ip::address_v4>, ServerListRequest::ParseError> ExtractAlternateIP(PacketIterator& start, const PacketIterator& end, ServerListRequest::Options options)
	{
		if (options & ServerListRequest::Options::alternate_source_ip) {
			return ExtractUInt32(start, end).transform([](auto ipValue) {
				return std::make_optional<boost::asio::ip::address_v4>(ipValue);
			});
		}

		return std::nullopt;
	}

	std::expected<std::optional<std::uint32_t>, ServerListRequest::ParseError> ExtractLimit(PacketIterator& start, const PacketIterator& end, ServerListRequest::Options options)
	{
		if (options & ServerListRequest::Options::limit_result_count) {
			return ExtractUInt32LE(start, end).transform([](auto limit) {
				return std::make_optional(limit);
			});
		}

		return std::nullopt;
	}
}

std::expected<ServerListRequest, ServerListRequest::ParseError> ServerListRequest::Parse(const std::span<const std::uint8_t>& packet) {
	constexpr auto client_challenge_length = decltype(ServerListRequest::challenge)::extent;
	constexpr auto MIN_SIZE =
		1ull /* protocol */ + 1ull /* encoding */ + 4ull /* gameversion (integer) */ +
		2ull /* from-gamename (min 1 byte + terminator) */ + 2ull /* to-gamename */ +
		client_challenge_length /* client challenge */ + 1ull /* query */ + 1ull /* field list (can be empty) */ +
		4ull /* options (integer) */; 
	
	if (packet.size() < MIN_SIZE)
		return std::unexpected(ParseError::insufficient_length);

	auto it = packet.begin();
	auto end = packet.end();

	auto protocol = static_cast<std::uint8_t>(*it++);
	if (protocol != ::list_protocol_version)
		return std::unexpected(ParseError::unknown_protocol_version);

	auto encoding = static_cast<std::uint8_t>(*it++);
	if (encoding != ::list_encoding_version)
		return std::unexpected(ParseError::unknown_encoding_version);
	
	auto gameversion = ExtractUInt32LE(it, end);
	if (!gameversion) return std::unexpected(gameversion.error());
	auto toGame = ExtractString(it, end);
	if (!toGame) return std::unexpected(toGame.error());
	auto fromGame = ExtractString(it, end);
	if (!fromGame) return std::unexpected(fromGame.error());
	if (fromGame->empty() || toGame->empty())
		return std::unexpected(ParseError::invalid_key);
	auto challenge = ExtractSpan<client_challenge_length>(it, end);
	if (!challenge) return std::unexpected(challenge.error());
	auto serverFilter = ExtractString(it, end, ::filter_total_length);
	if (!serverFilter) return std::unexpected(serverFilter.error());
	auto fieldList = ExtractFieldList(it, end);
	if (!fieldList) return std::unexpected(fieldList.error());
	auto options = ExtractOptions(it, end);
	if (!options) return std::unexpected(options.error());
	auto alternateIP = ExtractAlternateIP(it, end, *options);
	if (!alternateIP) return std::unexpected(alternateIP.error());
	auto limit = ExtractLimit(it, end, *options);
	if (!limit) return std::unexpected(limit.error());
	if (it != end)
		return std::unexpected(ParseError::invalid_key);

	return ServerListRequest{
		.protocolVersion = protocol,
		.encodingVersion = encoding,
		.fromGameVersion = *gameversion,
		.fromGame = *fromGame,
		.toGame = *toGame,
		.challenge = *challenge,
		.serverFilter = *serverFilter,
		.fieldList = std::move(*fieldList),
		.options = *options,
		.alternateSourceIP = *alternateIP,
		.limitResultCount = *limit
	};
}

std::vector<std::uint8_t> ServerListRequest::GetResponseHeaderBytes(const Game& forGame, const boost::asio::ip::address_v4& socketIP) const
{
	std::vector<std::uint8_t> response;

	response.append_range(socketIP.to_bytes());
	auto queryPort = forGame.queryPort();
	response.append_range(std::array{
		(queryPort >> 8) & 0xFF,
		(queryPort     ) & 0xFF
	});

	if (queryPort == std::numeric_limits<decltype(queryPort)>::max()) {
		response.append_range(std::string_view{ "Game does not support multiplayer!" });
		response.push_back(0);
	}

	if (options & ServerListRequest::Options::no_server_list || queryPort == 0xFFFF) {
		return response;
	}

	response.push_back(static_cast<std::uint8_t>(fieldList.size()));
	for (const auto& field : fieldList) {
		auto sendType = forGame.GetParamSendType(field);
		response.push_back(std::to_underlying(sendType));
		response.append_range(field);
		response.push_back(0);
	}

	// Popular values are not request specific and can be optimized:
	// They do not need to be generated for every request
	// (because of this, this function is not the ideal place to add them to the response) 
	// As they are hardly ever used this is currently not implemented nor optimized via hash-map
	const auto& popularValues = forGame.GetPopularValues();
	response.push_back(static_cast<std::uint8_t>(popularValues.size()));
	for (const auto& value : popularValues) {
		response.append_range(value);
		response.push_back(0);
	}

	return response;
}

namespace
{
	enum ServerOptions : std::uint8_t {
		unsolicited_udp = 1,
		private_ip = 2,
		connect_negotiate = 4,
		icmp_ip = 8,
		non_standard_port = 16,
		non_standard_private_port = 32,
		has_keys = 64,
		has_full_rules = 128,
		min = unsolicited_udp,
		max = has_full_rules
	};

	template<typename T>
	std::vector<std::uint8_t> PrepareServer(const Game& game, const Game::ServerData<T>& server, const std::vector<std::string_view>& fieldList, bool usePopularList)
	{
		std::vector<std::uint8_t> bytes;
		bytes.push_back(0); // flags
		bytes.front() |= ServerOptions::unsolicited_udp;

		static_assert(sizeof(boost::asio::ip::address_v4::bytes_type) == 4, "gamespy expects exactly 4 bytes as the public IP");
		bytes.append_range(boost::asio::ip::make_address_v4(server.public_ip).to_bytes());

		if (server.public_port != game.queryPort()) {
			bytes.front() |= ServerOptions::non_standard_port;
			bytes.append_range(std::array{
				static_cast<std::uint8_t>(server.public_port >> 8),
				static_cast<std::uint8_t>(server.public_port     )
			});
		}

		if (!server.private_ip.empty()) {
			bytes.front() |= ServerOptions::private_ip;
			bytes.append_range(boost::asio::ip::make_address_v4(server.private_ip).to_bytes());
		}

		if (server.private_port) {
			bytes.front() |= ServerOptions::non_standard_private_port;
			bytes.append_range(std::array{
				static_cast<std::uint8_t>(server.private_port >> 8),
				static_cast<std::uint8_t>(server.private_port     )
			});
		}

		if (const auto& natneg = server.data.find("natneg"); natneg != server.data.end()) {
			if (natneg->second == "1")
				bytes.front() |= ServerOptions::connect_negotiate;
		}

		if (!server.icmp_ip.empty()) {
			bytes.front() |= ServerOptions::icmp_ip;
			bytes.append_range(boost::asio::ip::make_address_v4(server.icmp_ip).to_bytes());
		}

		const auto& popularValues = game.GetPopularValues();
		if (!fieldList.empty())
			bytes.front() |= ServerOptions::has_keys;

		for (const auto& key : fieldList) {
			// because the value-list must match the key-list, we also need to send Send::no_send keys - but empty
			auto sendAs = game.GetParamSendType(key);
			using SendType = decltype(sendAs);

			auto entry = server.data.find(T{ key });
			if (entry == server.data.end())
				entry = std::ranges::find_if(server.data, [&](const auto& field) { return boost::beast::iequals(field.first, key); });
			const std::string_view value = entry == server.data.end() ? std::string_view{} : entry->second;
			switch (sendAs) {
			case SendType::as_string:
			{
				// instead of pushing the full value we can just add the values's position within the popular value list
				if (usePopularList) {
					auto popularValuePos = std::ranges::find(popularValues, value);
					if (popularValuePos != popularValues.end()) {
						bytes.push_back(static_cast<std::uint8_t>(std::ranges::distance(popularValues.begin(), popularValuePos)));
						break;
					}
				}

				// ParseServer(..., usepopularlist=0) reads pushed strings directly.
				if (usePopularList) bytes.push_back(0xFF);
				bytes.append_range(value);
				bytes.push_back(0x00);
				break;
			}
			case SendType::as_byte:
			{
				if (value.empty()) {
					bytes.push_back(0x00);
					break;
				}

				auto end = value.data() + value.size();
				std::uint8_t keyValue = 0;
				auto [ptr, ec] = std::from_chars(value.data(), end, keyValue);
				if (ec != std::errc{})
					throw std::system_error(std::make_error_code(ec));
				else if (ptr != end)
					throw std::runtime_error{ "Invalid byte value in server data" };
				bytes.push_back(keyValue);
				break;
			}
			case SendType::as_short:
			{
				if (value.empty()) {
					bytes.append_range(std::array{ 0x00, 0x00 });
					break;
				}

				auto end = value.data() + value.size();
				std::uint16_t keyValue = 0;
				auto [ptr, ec] = std::from_chars(value.data(), end, keyValue);
				if (ec != std::errc{})
					throw std::system_error(std::make_error_code(ec));
				else if (ptr != end)
					throw std::runtime_error{ "Invalid short value in server data" };

				bytes.append_range(std::array{
					static_cast<std::uint8_t>(keyValue >> 8),
					static_cast<std::uint8_t>(keyValue     )
				});
				break;				
			}
			default:
				throw std::out_of_range{ "Unknown SendType" };
			}
		}

		if (!server.rules.empty()) {
			bytes.front() |= ServerOptions::has_full_rules;
			bytes.append_range(server.rules);
			bytes.push_back(0x00);
		}

		return bytes;
	}
}

std::vector<std::uint8_t> ServerListRequest::GetServerBytes(const Game& game, const Game::IncomingServer& server, const std::vector<std::string_view>& fieldList, bool usePopularList)
{
	return ::PrepareServer(game, server, fieldList, usePopularList);
}

std::vector<std::uint8_t> ServerListRequest::GetServerBytes(const Game& game, const Game::SavedServer& server, const std::vector<std::string_view>& fieldList, bool usePopularList)
{
	return ::PrepareServer(game, server, fieldList, usePopularList);
}
