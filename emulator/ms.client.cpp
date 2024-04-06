#include "database.h"
#include "master.h"
#include "ms.client.h"
#include "sapphire.h"
#include "sl_request.h"
#include "utils.h"
#include <print>
using namespace gamespy;

BrowserClient::BrowserClient(boost::asio::ip::tcp::socket socket, MasterServer& master, Database& db)
	: m_Socket(std::move(socket)), m_Master(master), m_DB(db)
{

}

BrowserClient::~BrowserClient()
{

}

boost::asio::awaitable<void> BrowserClient::StartEncryption(const std::string_view& clientChallenge, const GameData& game)
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
	const auto& secretKey = game.GetSecretKey();
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
		if (error) {
			std::println("[browser] error: {}", error.what());
			break;
		}

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

	auto game = m_DB.GetGame(request->toGame);
	co_await StartEncryption(request->challenge, game);

	auto header = this->PrepareServerListHeader(game, *request);
	m_Cypher->encrypt(header);
	co_await m_Socket.async_send(boost::asio::buffer(header), boost::asio::use_awaitable);

	if (request->options & ServerListRequest::Options::NO_SERVER_LIST || game.GetQueryPort() == 0xFFFF)
		co_return;
	
	std::uint16_t limit = 500;
	if (request->limitResultCount)
		limit = std::min(static_cast<std::uint16_t>(*request->limitResultCount), limit);
	
	constexpr bool usePopularFields = true;
	std::vector<std::uint8_t> serverData;
	for (const auto& server : m_Master.GetServers(game.GetName(), request->serverFilter) | std::views::take(limit))
		serverData.append_range(PrepareServer(game, server, *request, usePopularFields));
	
	// Note: Server Data must be sent in one go because unfortunately there is a bug in the standard
	// gamespy limitation which will otherwise not perform a proper cleanup
	// (in ServerBrowserThink > SBListThink > ProcessIncomingData > CanReceiveOnSocket will not return 
	// true ever again and this way the client will never actually parse the "last server marker" and 
	// therefore never perform a cleanup)
	serverData.append_range(std::array{ 0x00, 0xFF, 0xFF, 0xFF, 0xFF });
	m_Cypher->encrypt(serverData);
	co_await m_Socket.async_send(boost::asio::buffer(serverData), boost::asio::use_awaitable);
}

std::vector<std::uint8_t> BrowserClient::PrepareServerListHeader(const GameData& game, const ServerListRequest& request)
{
	std::vector<std::uint8_t> response;

	response.append_range(m_Socket.remote_endpoint().address().to_v4().to_bytes());
	auto queryPort = game.GetQueryPort();
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
		response.push_back(std::to_underlying(game.GetKeyType(field)));
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

std::vector<std::uint8_t> BrowserClient::PrepareServer(const GameData& game, const ServerData& server, const ServerListRequest& request, bool usePopularValues)
{
	std::vector<std::uint8_t> response;

	response.push_back([&]() -> auto {
		enum Options : std::uint8_t {
			UNSOLICITED_UDP = 1 << 0,
			PRIVATE_IP = 1 << 1,
			CONNECT_NEGOTIATE = 1 << 2,
			ICMP_IP = 1 << 3,
			NON_STANDARD_PORT = 1 << 4,
			NON_STANDARD_PRIVATE_PORT = 1 << 5, // 0x20 / 32
			HAS_KEYS = 1 << 6,
			HAS_FULL_RULES = 1 << 7
		};

		auto flags = std::uint8_t{ 0 };
		if (server.public_port)
			flags |= Options::NON_STANDARD_PORT;

		if (server.private_ip)
			flags |= Options::PRIVATE_IP;

		if (server.private_port)
			flags |= Options::NON_STANDARD_PRIVATE_PORT;

		if (server.icmp_ip)
			flags |= Options::ICMP_IP;

		if (!server.data.empty())
			flags |= Options::HAS_KEYS;

		if (!server.stats.empty())
			flags |= Options::HAS_FULL_RULES;

		return flags;
	}());
	
	response.append_range(server.public_ip.to_v4().to_bytes());

	// NONSTANDARD_PORT_FLAG
	if (server.public_port)
		response.append_range(std::array{ 
			(*server.public_port >> 8) & 0xFF,
			(*server.public_port     ) & 0xFF
		});

	// PRIVATE_IP_FLAG
	if (server.private_ip)
		response.append_range(server.private_ip->to_v4().to_bytes());

	// NONSTANDARD_PRIVATE_PORT_FLAG
	if (server.private_port)
		response.append_range(std::array{
			(*server.private_port     ) & 0xFF,
			(*server.private_port >> 8) & 0xFF
		});
	
	// ICMP_IP_FLAG
	if (server.icmp_ip)
		response.append_range(server.icmp_ip->to_v4().to_bytes());

	const auto& popularValues = game.GetPopularValues();
	// HAS_KEYS_FLAG
	using KeyType = GameData::KeyType;
	for (const auto& key : request.fieldList) {
		KeyType keyType = game.GetKeyType(key);
		const auto& value = server.data.contains(key) ? server.data.at(key) : std::string{};
		switch (keyType) {
		case KeyType::STRING:
		{
			// instead of pushing the full value we can just add the values's position within the popular value list
			if (usePopularValues) {
				auto popularValuePos = std::ranges::find(popularValues, value);
				if (popularValuePos != popularValues.end()) {
					response.push_back(std::ranges::distance(popularValuePos, popularValues.end()) & 0xFF);
					break;
				}
			}

			response.push_back(0xFF);
			response.append_range(value);
			response.push_back(0x00);
			break;
		}
		case KeyType::BYTE:
			response.push_back(std::stoi(value) & 0xFF);
			break;
		case KeyType::SHORT:
		{
			auto keyValue = static_cast<std::uint16_t>(std::stoi(value));
			// Note: Little Endianess is expected
			response.append_range(std::array{
				(keyValue >> 8) & 0xFF,
				(keyValue     ) & 0xFF
			});
			break;
		}
		default:
			throw std::out_of_range{ "Unknown KeyType" };
		}
	}

	// HAS_FULL_RULES_FLAG
	if (!server.stats.empty()) {
		response.append_range(server.stats);
		response.push_back(0x00);
	}

	return response;
}