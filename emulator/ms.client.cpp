#include "gamedb.h"
#include "master.h"
#include "ms.client.h"
#include "sapphire.h"
#include "sb_request.h"
#include "utils.h"
#include <print>
#include <type_traits>
#include <boost/asio/experimental/awaitable_operators.hpp>
using namespace gamespy;

namespace {
	constexpr std::size_t crypt_key_length = 8;
	constexpr std::size_t server_challenge_length = 25;
	static_assert(crypt_key_length <= std::numeric_limits<std::uint8_t>::max(), "crypt_key_length must fit in a unsigned char");
	static_assert(server_challenge_length <= std::numeric_limits<std::uint8_t>::max(), "server_challenge_length must fit in a unsigned char");
	constexpr std::size_t gs_recv_buffer_size = 4096;

}

BrowserClient::BrowserClient(boost::asio::ip::tcp::socket socket, GameDB& db)
	: m_Socket(std::move(socket)), m_DB(db), m_SignalChannel(m_Socket.get_executor(), 10)
{

}

BrowserClient::~BrowserClient()
{

}

boost::asio::awaitable<void> BrowserClient::StartEncryption(const decltype(ServerListRequest::challenge)& clientChallenge, const Game& sourceGame)
{
	if (m_Cypher)
		co_return; // encryption already started

	auto cryptKey = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghjklmnopqrstuvwxyz0123456789", ::crypt_key_length);
	auto serverChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghjklmnopqrstuvwxyz0123456789", ::server_challenge_length);

	// GameSpy/serverbrowsing/sb_serverlist.c
	std::vector<std::uint8_t> data;
	data.push_back(((cryptKey.length() + 2) & 0xFF) ^ 0xEC);
	// gameoptions 
	const auto& gameOptions = std::to_underlying(m_Game->backend());
	data.append_range(std::array{
		(gameOptions >> 8) & 0xFF,
		(gameOptions     ) & 0xFF
	});
	data.append_range(cryptKey);
	data.push_back((serverChallenge.length() & 0xFF) ^ 0xEA);
	data.append_range(serverChallenge);

	auto key = std::vector<std::uint8_t>{ clientChallenge.begin(), clientChallenge.end() };
	const auto& keySize = key.size();
	const auto& secretKey = sourceGame.secretKey();
	const auto& secretKeySize = secretKey.length();
	for (std::string::size_type i = 0, size = serverChallenge.length(); i < size; i++)
		key[(i * secretKey[i % secretKeySize]) % keySize] ^= (key[i % keySize] ^ serverChallenge[i]);

	m_Cypher.emplace(key);

	co_await boost::asio::async_write(m_Socket, boost::asio::buffer(data), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> BrowserClient::Process()
{
	auto packet = co_await ReadPacket();
	if (!packet) co_return;
	if (packet->front() != std::to_underlying(ServerBrowsingPacket::Type::server_list_request)) {
		std::println("[browser] received initial packet of type {:2X} (expected server_list_request)", packet->front());
		co_return;
	}

	co_await HandleServerListRequest(std::span{ *packet }.subspan(1));
	if (!m_Socket.is_open()) co_return;

	// Only SendUpdates writes after the initial response. Keep the reader alive
	// across updates; cancel both operations only when one side finishes.
	using namespace boost::asio::experimental::awaitable_operators;
	co_await (ReadRequests() || SendUpdates());
}

boost::asio::awaitable<std::optional<std::vector<std::uint8_t>>> BrowserClient::ReadPacket()
{
	std::array<std::uint8_t, 2> header;
	auto [error, length] = co_await boost::asio::async_read(m_Socket, boost::asio::buffer(header), boost::asio::as_tuple(boost::asio::use_awaitable));
	if (error) co_return std::nullopt;

	const auto packetLength = (std::size_t{ header[0] } << 8) | header[1];
	if (packetLength < 3 || packetLength > gs_recv_buffer_size) {
		std::println("[browser] invalid packet length {}", packetLength);
		co_return std::nullopt;
	}

	std::vector<std::uint8_t> packet(packetLength - header.size());
	auto [bodyError, bodyLength] = co_await boost::asio::async_read(m_Socket, boost::asio::buffer(packet), boost::asio::as_tuple(boost::asio::use_awaitable));
	if (bodyError) co_return std::nullopt;
	if (packet.front() > std::to_underlying(ServerBrowsingPacket::Type::max)) {
		std::println("[browser] invalid packet type {:2X}", packet.front());
		co_return std::nullopt;
	}
	co_return packet;
}

boost::asio::awaitable<void> BrowserClient::ReadRequests()
{
	while (auto packet = co_await ReadPacket()) {
		// Further request types are not implemented; BF2 disconnects after the list.
		std::println("[browser] received unhandled packet {:2X}", packet->front());
	}
}

boost::asio::awaitable<void> BrowserClient::SendUpdates()
{
	while (m_Socket.is_open()) {
		auto [error, bytes] = co_await m_SignalChannel.async_receive(boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error) co_return;
		m_Cypher->encrypt(bytes);
		auto [writeError, length] = co_await boost::asio::async_write(m_Socket, boost::asio::buffer(bytes), boost::asio::as_tuple(boost::asio::use_awaitable));
		if (writeError) co_return;
	}
}

void BrowserClient::QueueUpdate(std::uint8_t type, std::vector<std::uint8_t> bytes)
{
	// SDK ProcessAdHocData: network-order total length, type, then payload.
	if (bytes.size() <= std::numeric_limits<std::uint16_t>::max() - 3) {
		const auto length = bytes.size() + 3;
		bytes.insert(bytes.begin(), { static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length), type });
		if (m_SignalChannel.try_send(boost::system::error_code{}, std::move(bytes))) return;
	}

	// A slow client must reconnect for a fresh list instead of silently losing updates.
	boost::system::error_code ignored;
	m_Socket.close(ignored);
	m_SignalChannel.close();
}

boost::asio::awaitable<void> BrowserClient::HandleServerListRequest(const std::span<const std::uint8_t>& bytes)
{
	const auto request = ServerListRequest::Parse(bytes);
	if (!request) {
		std::println("[browser] packet of type SERVER_LIST_REQUEST is invalid {}", std::to_underlying(request.error()));
		m_Socket.close();
		co_return;
	}

	if (!co_await m_DB.HasGame(request->toGame) || !co_await m_DB.HasGame(request->fromGame)) {
		std::println("[browser] unknown source or target game: {} -> {}", request->fromGame, request->toGame);
		m_Socket.close();
		co_return;
	}

	m_Game = co_await m_DB.GetGame(request->toGame);
	// SDK InitCryptKey uses queryfromkey, even when browsing another game.
	auto sourceGame = co_await m_DB.GetGame(request->fromGame);
	co_await StartEncryption(request->challenge, *sourceGame);

	co_await Send(request->GetResponseHeaderBytes(*m_Game, m_Socket.remote_endpoint().address().to_v4()));

	using Options = ServerListRequest::Options;
	if (request->options & Options::no_server_list || m_Game->queryPort() == 0xFFFF)
		co_return;

	const std::array<std::uint8_t, 5> endMarker{ 0, 255, 255, 255, 255 };
	if (request->options & Options::send_groups) {
		// Group listings are not implemented; finish the empty list for SDK clients.
		co_await Send(endMarker);
		co_return;
	}
	
	// a non-pushed server list is expected to contain the popular fields
	auto limit = std::min<std::uint32_t>(request->limitResultCount.value_or(500), 500);
	for (auto& server : co_await m_Game->GetServers(request->serverFilter, request->fieldList, limit)) {
		constexpr bool usePopularFields = true;
		co_await Send(ServerListRequest::GetServerBytes(*m_Game, server, request->fieldList, usePopularFields));
	}

	co_await Send(endMarker);

	// configure adhoc updates
	if (request->options & Options::push_updates) {
		// Reserve before making views: moving short strings invalidates their views.
		m_KeyListStorage.reserve(request->fieldList.size());
		m_KeyList.reserve(request->fieldList.size());
		for (const auto& field : request->fieldList) {
			const auto& fieldStr = m_KeyListStorage.emplace_back(field);
			m_KeyList.emplace_back(fieldStr);
		}

		m_OnServerAdded = m_Game->OnServerAdded.connect([this](const Game::IncomingServer& server) {
			QueueUpdate(0x02, ServerListRequest::GetServerBytes(*m_Game, server, m_KeyList, false));
		});

		m_OnServerRemoved = m_Game->OnServerRemoved.connect([this](const std::string_view& ip, std::uint16_t port) {
			std::vector<std::uint8_t> bytes;
			bytes.append_range(boost::asio::ip::make_address_v4(ip).to_bytes());
			bytes.append_range(std::array{
				static_cast<std::uint8_t>(port >> 8),
				static_cast<std::uint8_t>(port)
				});
			QueueUpdate(0x04, std::move(bytes));
		});
	}
}
