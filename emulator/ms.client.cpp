#include "gamedb.h"
#include "master.h"
#include "ms.client.h"
#include "sapphire.h"
#include "utils.h"
#include <print>
#include <type_traits>
using namespace gamespy;

namespace {
	constexpr std::uint8_t list_protocol_version = 1;
	constexpr std::uint8_t list_encoding_version = 3;
	constexpr std::size_t crypt_key_length = 8;
	constexpr std::size_t server_challenge_length = 25;
	static_assert(crypt_key_length <= std::numeric_limits<std::uint8_t>::max(), "crypt_key_length must fit in a unsigned char");
	static_assert(server_challenge_length <= std::numeric_limits<std::uint8_t>::max(), "server_challenge_length must fit in a unsigned char");

	enum ServerOptions : std::uint8_t {
		unsolicited_udp           = 1,
		private_ip                = 2,
		connect_negotiate         = 4,
		icmp_ip                   = 8,
		non_standard_port         = 16,
		non_standard_private_port = 32,
		has_keys                  = 64,
		has_full_rules            = 128
	};

	enum class RequestType : std::uint8_t {
		server_list_request = 0,
		server_info_request,
		send_message_request,
		keepalive_request,
		maploop_request,
		playersearch_request
	};
}

BrowserClient::BrowserClient(boost::asio::ip::tcp::socket socket, GameDB& db)
	: m_Socket(std::move(socket)), m_DB(db)
{

}

BrowserClient::~BrowserClient()
{

}

boost::asio::awaitable<void> BrowserClient::StartEncryption(const std::string_view& clientChallenge, const Game& game)
{
	if (m_Cypher)
		co_return; // encryption already started

	auto cryptKey = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghjklmnopqrstuvwxyz0123456789", ::crypt_key_length);
	auto serverChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghjklmnopqrstuvwxyz0123456789", ::server_challenge_length);

	// GameSpy/serverbrowsing/sb_serverlist.c
	std::vector<std::uint8_t> data;
	data.push_back(((cryptKey.length() + 2) & 0xFF) ^ 0xEC);
	// gameoptions 
	const auto& gameOptions = std::to_underlying(game.backend());
	data.append_range(std::array{
		(gameOptions >> 8) & 0xFF,
		(gameOptions     ) & 0xFF
	});
	data.append_range(cryptKey);
	data.push_back((serverChallenge.length() & 0xFF) ^ 0xEA);
	data.append_range(serverChallenge);

	auto key = std::vector<std::uint8_t>{ clientChallenge.begin(), clientChallenge.end() };
	const auto& keySize = key.size();
	const auto& secretKey = game.secretKey();
	const auto& secretKeySize = secretKey.length();
	for (std::string::size_type i = 0, size = serverChallenge.length(); i < size; i++)
		key[(i * secretKey[i % secretKeySize]) % keySize] ^= (key[i % keySize] ^ serverChallenge[i]);

	m_Cypher.emplace(key);

	co_await m_Socket.async_send(boost::asio::buffer(data), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> BrowserClient::Process()
{
	auto endpoint = m_Socket.local_endpoint();
	auto buffer = boost::asio::streambuf{};
	auto mutableBuffer = buffer.prepare(1024);

	while (m_Socket.is_open()) {
		const auto& [error, length] = co_await m_Socket.async_read_some(mutableBuffer, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			break;

		buffer.commit(length);
		if (buffer.size() < 3) {
			continue;
		}

		auto packet = std::span<const std::uint8_t>{ boost::asio::buffer_cast<const std::uint8_t*>(buffer.data()), buffer.size() };
		std::uint16_t packetLength = (static_cast<std::uint16_t>(packet[0]) << 8) | static_cast<std::uint16_t>(packet[1]);
		if (packetLength < buffer.size())
			continue; // full packet was not yet received

		switch (static_cast<RequestType>(packet[2])) {
		case RequestType::server_list_request:
			co_await HandleServerListRequest(packet.subspan(3));
			break;
		default:
			std::println("[browser] received unknown packet {:2X}", packet[2]);
		}

		buffer.consume(packetLength);
	}
}

boost::asio::awaitable<void> BrowserClient::HandleServerListRequest(const std::span<const std::uint8_t>& bytes)
{
	const auto request = ServerListRequest::Parse(bytes);
	if (!request) {
		m_Socket.close();
		std::println("[browser] packet of type SERVER_LIST_REQUEST is invalid {}", std::to_underlying(request.error()));
		co_return;
	}

	if (!co_await m_DB.HasGame(request->toGame)) {
		m_Socket.close();
		std::println("[browser] unknown game {}", request->toGame);
		co_return;
	}

	const auto& game = co_await m_DB.GetGame(request->toGame);
	co_await StartEncryption(request->challenge, *game);

	auto header = PrepareServerListHeader(*game, *request);
	m_Cypher->encrypt(header);
	co_await m_Socket.async_send(boost::asio::buffer(header), boost::asio::use_awaitable);

	using Options = ServerListRequest::Options;
	if (request->options & Options::no_server_list || game->queryPort() == 0xFFFF)
		co_return;

	if (request->options & Options::send_groups)
		co_return; // not yet implemented
	
	std::uint16_t limit = 500;
	if (request->limitResultCount)
		limit = std::min(static_cast<std::uint16_t>(*request->limitResultCount), limit);
	
	constexpr bool usePopularFields = true;
	std::vector<std::uint8_t> serverData;
	for (auto& server : co_await game->GetServers(request->serverFilter, request->fieldList, limit)) {
		server.data["bf2_plasma"] = "1";
		server.data["bf2_pure"] = "1";
		serverData.append_range(PrepareServer(*game, server, *request, usePopularFields));
	}

	m_Cypher->encrypt(serverData);
	co_await m_Socket.async_send(boost::asio::buffer(serverData), boost::asio::use_awaitable);
	serverData.clear();
	
	// Note: Server Data must be sent in one go because unfortunately there is a bug in the standard
	// gamespy implementation:
	// ServerBrowserThink > SBListThink > ProcessIncomingData > CanReceiveOnSocket will not return true
	// ever again and this way the client will never actually parse the "last server marker" and 
	// therefore never perform a cleanup
	// Update 01/2025: It seems that at least BF2 can handle this scenario
	serverData.append_range(std::array{ 0x00, 0xFF, 0xFF, 0xFF, 0xFF });
	m_Cypher->encrypt(serverData);
	co_await m_Socket.async_send(boost::asio::buffer(serverData), boost::asio::use_awaitable);
}

std::vector<std::uint8_t> BrowserClient::PrepareServerListHeader(const Game& game, const ServerListRequest& request)
{
	std::vector<std::uint8_t> response;

	response.append_range(m_Socket.remote_endpoint().address().to_v4().to_bytes());
	auto queryPort = game.queryPort();
	response.append_range(std::array{
		(queryPort >> 8) & 0xFF,
		(queryPort     ) & 0xFF
	});

	if (queryPort == 0xFFFF) {
		response.append_range("Game does not support multiplayer!");
		response.push_back(0);
	}

	if (request.options & ServerListRequest::Options::no_server_list || queryPort == 0xFFFF) {
		return response;
	}

	if (request.fieldList.size() >= 0xFF)
		throw std::overflow_error{ "too many fields" };

	response.push_back(request.fieldList.size() & 0xFF);
	for (const auto& field : request.fieldList) {
		response.push_back(std::to_underlying(game.GetParamType(field)));
		response.append_range(field);
		response.push_back(0);
	}

	// Popular values are not request specific and can be optimized:
	// They do not need to be generated for every request
	// (because of this, this function is not the ideal place to add them to the response) 
	// As they are hardly ever used this is currently not implemented nor optimized via hash-map
	const auto& popularValues = game.GetPopularValues();
	response.push_back(popularValues.size() & 0xFF);
	for (const auto& value : popularValues) {
		response.append_range(value);
		response.push_back(0);
	}

	return response;
}

std::vector<std::uint8_t> BrowserClient::PrepareServer(const Game& game, const Game::SavedServer& server, const ServerListRequest& request, bool usePopularValues)
{
	std::vector<std::uint8_t> bytes;
	bytes.push_back(0); // flags
	bytes.front() |= ServerOptions::unsolicited_udp;

	static_assert(sizeof(boost::asio::ip::address_v4::bytes_type) == 4, "gamespy expects exactly 4 bytes as the public IP");
	bytes.append_range(boost::asio::ip::make_address_v4(server.public_ip).to_bytes());

	if (server.public_port != game.queryPort()) {
		bytes.front() |= ServerOptions::non_standard_port;
		bytes.append_range(std::array{
			(server.public_port >> 8) & 0xFF,
			(server.public_port) & 0xFF
		});
	}

	if (!server.private_ip.empty()) {
		bytes.front() |= ServerOptions::private_ip;
		bytes.append_range(boost::asio::ip::make_address_v4(server.private_ip).to_bytes());
	}

	if (server.private_port) {
		bytes.front() |= ServerOptions::non_standard_private_port;
		bytes.append_range(std::array{
			(server.private_port >> 8) & 0xFF,
			(server.private_port) & 0xFF
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
	if (!server.data.empty())
		bytes.front() |= ServerOptions::has_keys;

	for (const auto& key : request.fieldList) {
		auto keyType = game.GetParamType(key);
		using SendType = decltype(keyType);

		const auto& value = server.data.contains(key) ? server.data.at(key) : std::string{};
		switch (keyType) {
		case SendType::as_string:
		{
			// instead of pushing the full value we can just add the values's position within the popular value list
			if (usePopularValues) {
				auto popularValuePos = std::ranges::find(popularValues, value);
				if (popularValuePos != popularValues.end()) {
					bytes.push_back(std::ranges::distance(popularValuePos, popularValues.end()) & 0xFF);
					break;
				}
			}

			bytes.push_back(0xFF);
			bytes.append_range(value);
			bytes.push_back(0x00);
			break;
		}
		case SendType::as_byte:
			bytes.push_back(std::stoi(value) & 0xFF);
			break;
		case SendType::as_short:
		{
			auto keyValue = static_cast<std::uint16_t>(std::stoi(value));
			bytes.append_range(std::array{
				(keyValue >> 8) & 0xFF,
				(keyValue     ) & 0xFF
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

std::string_view ExtractString(const ServerListRequest::bytes& packet, ServerListRequest::bytes::iterator& start)
{
	const auto pkgEnd = packet.end();
	const auto strEnd = std::find(start, pkgEnd, '\0');
	if (strEnd == pkgEnd)
		throw ServerListRequest::ParseError::insufficient_length;

	auto res = std::string_view{ reinterpret_cast<const char*>(&*start), reinterpret_cast<const char*>(&*strEnd) };
	start = strEnd + 1;
	return res;
}

std::uint32_t ExtractUInt32(const ServerListRequest::bytes& packet, ServerListRequest::bytes::iterator& start)
{
	if (std::distance(start, packet.end()) < 4)
		throw ServerListRequest::ParseError::insufficient_length;

	return static_cast<std::uint32_t>((*start++ << 24) | (*start++ << 16) | (*start++ << 8) | *start++);
}

std::expected<ServerListRequest, ServerListRequest::ParseError> ServerListRequest::Parse(const ServerListRequest::bytes& packet) {
	constexpr std::size_t MIN_SIZE = 
		1 /* protocol */ + 1 /* encoding */ + 4 /* gameversion (integer) */ +
		2 /* from-gamename (min 1 byte + terminator) */ + 2 /* to-gamename */ +
		client_challenge_length /* client challenge */ + 1 /* query */ + 3 /* field list (\\-prefix + 1 byte + terminator) */ +
		4 /* options (integer) */;
	if (packet.size() < MIN_SIZE)
		return std::unexpected(ParseError::insufficient_length);

	auto packetIter = packet.begin();

	auto protocol = static_cast<std::uint8_t>(*packetIter++);
	if (protocol != ::list_protocol_version)
		return std::unexpected(ParseError::unknown_protocol_version);

	auto encoding = static_cast<std::uint8_t>(*packetIter++);
	if (encoding != ::list_encoding_version)
		return std::unexpected(ParseError::unknown_encoding_version);

	// yup, thats right: exceptions used for control flow but they make the code much more readable and
	// can hopefully be replaced by a operator-try in the future
	try {
		const auto gameversion = ExtractUInt32(packet, packetIter);
		const auto fromGame = ExtractString(packet, packetIter);
		const auto toGame = ExtractString(packet, packetIter);

		// the query is always CHALLENGE_LENGTH (8) bytes long, *not* null terminated (!) and 
		// followed by the server-list-query which is null-terminated, but might be empty
		auto challenge = ExtractString(packet, packetIter);
		if (challenge.length() < client_challenge_length)
			return std::unexpected(ParseError::insufficient_length);

		const auto query = challenge.substr(client_challenge_length);
		challenge = challenge.substr(0, client_challenge_length);

		auto fields = std::vector<std::string_view>{};
		{
			auto fieldStr = ExtractString(packet, packetIter);
			if (!fieldStr.empty() && fieldStr.front() != '\\')
				return std::unexpected(ParseError::invalid_key);

			auto fieldNames = fieldStr.substr(1) // fields always start with '\\' which we need to remove before the split so we don't get an empty string
				| std::views::split('\\')
				| std::views::transform([](const auto& range) { return std::string_view{ range.begin(), range.end() }; });

			fields.insert(fields.end(), std::make_move_iterator(fieldNames.begin()), std::make_move_iterator(fieldNames.end()));
			if (fields.size() >= 255)
				return std::unexpected(ParseError::too_many_keys);
		}

		const auto options = ExtractUInt32(packet, packetIter);

		auto alternateIP = std::optional<boost::asio::ip::address_v4>{};
		if (options & std::to_underlying(Options::alternate_source_ip))
			alternateIP.emplace(ExtractUInt32(packet, packetIter));

		auto limit = std::optional<std::uint32_t>{};
		if (options & std::to_underlying(Options::limit_result_count))
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

namespace {
#include <GameSpy/serverbrowsing/sb_internal.h>
	static_assert(::list_protocol_version == LIST_PROTOCOL_VERSION, "LIST_PROTOCOL_VERSION value missmatch");
	static_assert(::list_encoding_version == LIST_ENCODING_VERSION, "LIST_ENCODING_VERSION value missmatch");

	// check Request Options bounds
	static_assert(std::to_underlying(::RequestType::server_list_request) == SERVER_LIST_REQUEST, "SERVER_LIST_REQUEST value missmatch");
	static_assert(std::to_underlying(::RequestType::playersearch_request) == PLAYERSEARCH_REQUEST, "PLAYERSEARCH_REQUEST value missmatch");

	static_assert(ServerListRequest::client_challenge_length == LIST_CHALLENGE_LEN, "the crypt key length must match the gamespy definition");

	// check ServerListRequest Options bounds
	static_assert(std::to_underlying(ServerListRequest::Options::send_fields_for_all) == SEND_FIELDS_FOR_ALL, "SEND_FIELDS_FOR_ALL value missmatch");
	static_assert(std::to_underlying(ServerListRequest::Options::limit_result_count) == LIMIT_RESULT_COUNT, "SEND_FIELDS_FOR_ALL value missmatch");

	// check Server Options bounds
	static_assert(std::to_underlying(::ServerOptions::unsolicited_udp) == UNSOLICITED_UDP_FLAG, "UNSOLICITED_UDP_FLAG value missmatch");
	static_assert(std::to_underlying(::ServerOptions::has_full_rules) == HAS_FULL_RULES_FLAG, "HAS_FULL_RULES_FLAG value missmatch");

	// check Backend Options bounds
	static_assert(std::to_underlying(GameData::BackendOptions::qr2_use_query_challenge) == QR2_USE_QUERY_CHALLENGE, "QR2_USE_QUERY_CHALLENGE value missmatch");
}
