#include "gamedb.h"
#include "master.h"
#include "ms.client.h"
#include "sapphire.h"
#include "utils.h"
#include <print>
using namespace gamespy;

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

	auto obfuscation = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghjklmnopqrstuvwxyz0123456789", 8);
	auto serverChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghjklmnopqrstuvwxyz0123456789", 25);

	std::vector<std::uint8_t> data;
	data.push_back(((obfuscation.length() + 2) & 0xFF) ^ 0xEC);
	data.append_range(std::array{ 0,0 }); // gameoptions 
	data.append_range(obfuscation);
	data.push_back((serverChallenge.length() & 0xFF) ^ 0xEA);
	data.append_range(serverChallenge);

	auto key = std::vector<std::uint8_t>{ clientChallenge.begin(), clientChallenge.end() };
	const auto& keySize = key.size();
	const auto& secretKey = game.Data().secretKey;
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

		enum class RequestType : std::uint8_t {
			SERVER_LIST_REQUEST = 0,
			SERVER_INFO_REQUEST,
			SEND_MESSAGE_REQUEST,
			KEEPALIVE_REPLY,
			MAPLOOP_REQUEST,
			PLAYERSEARCH_REQUEST
		};

		switch (static_cast<RequestType>(packet[2])) {
		case RequestType::SERVER_LIST_REQUEST:
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

	if (!m_DB.HasGame(request->toGame)) {
		m_Socket.close();
		std::println("[browser] unknown game {}", request->toGame);
		co_return;
	}

	auto& game = m_DB.GetGame(request->toGame);
	co_await StartEncryption(request->challenge, game);

	auto header = PrepareServerListHeader(game, *request);
	m_Cypher->encrypt(header);
	co_await m_Socket.async_send(boost::asio::buffer(header), boost::asio::use_awaitable);

	using Options = ServerListRequest::Options;
	if (request->options & Options::NO_SERVER_LIST || game.Data().queryPort == 0xFFFF)
		co_return;

	if (request->options & Options::SEND_GROUPS)
		co_return; // not yet implemented
	
	std::uint16_t limit = 500;
	if (request->limitResultCount)
		limit = std::min(static_cast<std::uint16_t>(*request->limitResultCount), limit);
	
	constexpr bool usePopularFields = true;
	std::vector<std::uint8_t> serverData;
	for (auto& server : game.GetServers(request->serverFilter, request->fieldList, limit)) {
		server.data["bf2_plasma"] = "1";
		server.data["bf2_pure"] = "1";
		serverData.append_range(PrepareServer(game, server, *request, usePopularFields));
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
	auto queryPort = game.Data().queryPort;
	response.append_range(std::array{
		(queryPort >> 8) & 0xFF,
		(queryPort     ) & 0xFF
	});

	if (queryPort == 0xFFFF) {
		response.append_range("Game does not support multiplayer!");
		response.push_back(0);
	}

	if (request.options & ServerListRequest::Options::NO_SERVER_LIST || queryPort == 0xFFFF) {
		return response;
	}

	if (request.fieldList.size() >= 0xFF)
		throw std::overflow_error{ "too many fields" };

	response.push_back(request.fieldList.size() & 0xFF);
	for (const auto& field : request.fieldList) {
		response.push_back(std::to_underlying(game.Data().GetKeyType(field)));
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

std::vector<std::uint8_t> BrowserClient::PrepareServer(const Game& game, const Game::Server& server, const ServerListRequest& request, bool usePopularValues)
{
	std::vector<std::uint8_t> bytes;
	bytes.push_back(0); // flags

	enum Options : std::uint8_t {
		UNSOLICITED_UDP           = 1,
		PRIVATE_IP                = 2,
		CONNECT_NEGOTIATE         = 4,
		ICMP_IP                   = 8,
		NON_STANDARD_PORT         = 16,
		NON_STANDARD_PRIVATE_PORT = 32,
		HAS_KEYS                  = 64,
		HAS_FULL_RULES            = 128
	};

	bytes.front() |= Options::UNSOLICITED_UDP;

	bytes.append_range(boost::asio::ip::make_address_v4(server.public_ip).to_bytes());

	if (server.public_port != game.Data().queryPort) {
		bytes.front() |= Options::NON_STANDARD_PORT;
		bytes.append_range(std::array{
			(server.public_port >> 8) & 0xFF,
			(server.public_port) & 0xFF
		});
	}

	if (!server.private_ip.empty()) {
		bytes.front() |= Options::PRIVATE_IP;
		bytes.append_range(boost::asio::ip::make_address_v4(server.private_ip).to_bytes());
	}

	if (server.private_port) {
		bytes.front() |= Options::NON_STANDARD_PRIVATE_PORT;
		bytes.append_range(std::array{
			(server.private_port >> 8) & 0xFF,
			(server.private_port) & 0xFF
		});
	}

	if (const auto& natneg = server.data.find("natneg"); natneg != server.data.end()) {
		if (natneg->second == "1")
			bytes.front() |= Options::CONNECT_NEGOTIATE;
	}
	
	if (!server.icmp_ip.empty()) {
		bytes.front() |= Options::ICMP_IP;
		bytes.append_range(boost::asio::ip::make_address_v4(server.icmp_ip).to_bytes());
	}

	const auto& popularValues = game.GetPopularValues();
	if (!server.data.empty())
		bytes.front() |= Options::HAS_KEYS;

	using KeyType = GameData::KeyType;
	for (const auto& key : request.fieldList) {
		KeyType keyType = game.Data().GetKeyType(key);
		const auto& value = server.data.contains(key) ? server.data.at(key) : std::string{};
		switch (keyType) {
		case KeyType::STRING:
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
		case KeyType::BYTE:
			bytes.push_back(std::stoi(value) & 0xFF);
			break;
		case KeyType::SHORT:
		{
			auto keyValue = static_cast<std::uint16_t>(std::stoi(value));
			// Note: Little Endianess is expected
			bytes.append_range(std::array{
				(keyValue >> 8) & 0xFF,
				(keyValue     ) & 0xFF
			});
			break;
		}
		default:
			throw std::out_of_range{ "Unknown KeyType" };
		}
	}

	if (!server.stats.empty()) {
		bytes.front() |= Options::HAS_FULL_RULES;
		bytes.append_range(server.stats);
		bytes.push_back(0x00);
	}

	return bytes;
}

std::string ExtractString(const ServerListRequest::bytes& packet, ServerListRequest::bytes::iterator& start)
{
	const auto pkgEnd = packet.end();
	const auto strEnd = std::find(start, pkgEnd, '\0');
	if (strEnd == pkgEnd)
		throw ServerListRequest::ParseError::INSUFFICIENT_LENGTH;

	auto res = std::string{ start, strEnd };
	start = strEnd + 1;
	return res;
}

std::uint32_t ExtractUInt32(const ServerListRequest::bytes& packet, ServerListRequest::bytes::iterator& start)
{
	if (std::distance(start, packet.end()) < 4)
		throw ServerListRequest::ParseError::INSUFFICIENT_LENGTH;

	return static_cast<std::uint32_t>((*start++ << 24) | (*start++ << 16) | (*start++ << 8) | *start++);
}

std::expected<ServerListRequest, ServerListRequest::ParseError> ServerListRequest::Parse(const ServerListRequest::bytes& packet) {
	constexpr std::size_t MIN_SIZE = 
		1 /* protocol */ + 1 /* encoding */ + 4 /* gameversion (integer) */ +
		2 /* from-gamename (min 1 byte + terminator) */ + 2 /* to-gamename */ +
		CHALLENGE_LENGTH /* client challenge */ + 1 /* query */ + 3 /* field list (\\-prefix + 1 byte + terminator) */ +
		4 /* options (integer) */;
	if (packet.size() < MIN_SIZE)
		return std::unexpected(ParseError::INSUFFICIENT_LENGTH);

	auto packetIter = packet.begin();

	auto protocol = static_cast<std::uint8_t>(*packetIter++);
	if (protocol != 0x01)
		return std::unexpected(ParseError::UNKNOWN_PROTOCOL_VERSION);

	auto encoding = static_cast<std::uint8_t>(*packetIter++);
	if (encoding != 0x03)
		return std::unexpected(ParseError::UNKNOWN_ENCODING_VERSION);

	// yup, thats right: exceptions used for control flow but they make the code much more readable and
	// can hopefully be replaced by a operator-try in the future
	try {
		const auto gameversion = ExtractUInt32(packet, packetIter);
		const auto fromGame = ExtractString(packet, packetIter);
		const auto toGame = ExtractString(packet, packetIter);

		// the query is always CHALLENGE_LENGTH (8) bytes long, *not* null terminated (!) and 
		// followed by the server-list-query which is null-terminated, but might be empty
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