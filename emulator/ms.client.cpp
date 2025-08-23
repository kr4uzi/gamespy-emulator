#include "gamedb.h"
#include "master.h"
#include "ms.client.h"
#include "sapphire.h"
#include "sb_request.h"
#include "utils.h"
#include <print>
#include <type_traits>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/coro.hpp>
using namespace gamespy;

namespace {
	constexpr std::size_t crypt_key_length = 8;
	constexpr std::size_t server_challenge_length = 25;
	static_assert(crypt_key_length <= std::numeric_limits<std::uint8_t>::max(), "crypt_key_length must fit in a unsigned char");
	static_assert(server_challenge_length <= std::numeric_limits<std::uint8_t>::max(), "server_challenge_length must fit in a unsigned char");
	constexpr std::size_t gs_recv_buffer_size = 4096;

	boost::asio::experimental::coro<ServerBrowsingPacket> packet_reader(boost::asio::ip::tcp::socket& sock)
	{
		auto buffer = std::vector<std::uint8_t>(::gs_recv_buffer_size);
		auto available = std::size_t{ 0 };
		while (sock.is_open())
		{
			const auto& [error, length] = co_await sock.async_read_some(boost::asio::buffer(buffer.data() + available, buffer.size() - available), boost::asio::as_tuple);
			if (error) break;
			available += length;
			auto offset = std::size_t{ 0 };
			while (available - offset >= 3) {
				auto packetLength = (std::size_t(buffer[offset]) << 8) | std::size_t(buffer[offset + 1]);
				if (packetLength > available - offset)
					break; // full packet was not yet received

				auto bytes = std::span(buffer.data() + offset, packetLength);
				auto type = bytes[2];
				if (type < std::to_underlying(ServerBrowsingPacket::Type::min) || type > std::to_underlying(ServerBrowsingPacket::Type::max)) {
					std::println("[browser] received invalid packet {:2X}", bytes[2]);
					co_return;
				}

				co_yield ServerBrowsingPacket{
					.type = static_cast<ServerBrowsingPacket::Type>(type),
					.data = bytes.subspan(3)
				};

				offset += packetLength;
			}

			if (offset) {
				std::memmove(buffer.data(), buffer.data() + offset, available - offset);
				available -= offset;
			}
		}
	}

	boost::asio::experimental::coro<std::vector<std::uint8_t>> channel_reader(boost::asio::ip::tcp::socket& sock, boost::asio::experimental::channel<void(boost::system::error_code, std::vector<std::uint8_t>)>& channel)
	{
		while (sock.is_open()) {
			const auto& [error, bytes] = co_await channel.async_receive(boost::asio::as_tuple);
			if (error) break;
			co_yield bytes;
		}
	}
}

BrowserClient::BrowserClient(boost::asio::ip::tcp::socket socket, GameDB& db)
	: m_Socket(std::move(socket)), m_DB(db), m_SignalChannel(m_Socket.get_executor(), 10)
{

}

BrowserClient::~BrowserClient()
{

}

boost::asio::awaitable<void> BrowserClient::StartEncryption(const decltype(ServerListRequest::challenge)& clientChallenge, const Game& game)
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
	auto reader = ::packet_reader(m_Socket);
	
	auto _packet = co_await reader.async_resume(boost::asio::use_awaitable);
	if (!_packet) co_return;
	auto& packet = *_packet;
	if (packet.type != ServerBrowsingPacket::Type::server_list_request) {
		std::println("[browser] received initial packet of type {:2X} (expected server_list_request)", std::to_underlying(packet.type));
		co_return;
	}

	co_await HandleServerListRequest(packet.data);

	auto adhock = ::channel_reader(m_Socket, m_SignalChannel);
	while (m_Socket.is_open()) {
		// do not use two coroutines here because we do not want to interleave sending data from the channel and
		// sending data generated from the packet handling
		using namespace boost::asio::experimental::awaitable_operators;
		auto incoming = co_await (
			adhock.async_resume(boost::asio::use_awaitable)
			|| reader.async_resume(boost::asio::use_awaitable)
		);

		if (incoming.index() == 0) {
			// adhoc data (server added/removed)
			auto& _bytes = std::get<0>(incoming);
			if (!_bytes) break;

			auto& bytes = *_bytes;
			m_Cypher->encrypt(bytes);
			co_await m_Socket.async_send(boost::asio::buffer(bytes), boost::asio::use_awaitable);
		}

		auto& _packet = std::get<1>(incoming);
		if (!_packet) break;
		const auto& packet = *_packet;

		// TODO: bf2 is using the serverbrowsing with disconnectOnComplete=true, so no other types are expected
		// I figured this out *after* implementing the adhoc (server added/removed) mechanism ...
		std::println("[browser] received unhandled packet {:2X}", std::to_underlying(packet.type));
	}
}

boost::asio::awaitable<void> BrowserClient::HandleServerListRequest(const std::span<const std::uint8_t>& bytes)
{
	const auto request = ServerListRequest::Parse(bytes);
	if (!request) {
		std::println("[browser] packet of type SERVER_LIST_REQUEST is invalid {}", std::to_underlying(request.error()));
		m_Socket.close();
		co_return;
	}

	if (!co_await m_DB.HasGame(request->toGame)) {
		std::println("[browser] unknown game {}", request->toGame);
		m_Socket.close();
		co_return;
	}

	m_Game = co_await m_DB.GetGame(request->toGame);
	co_await StartEncryption(request->challenge, *m_Game);

	co_await Send(request->GetResponseHeaderBytes(*m_Game, m_Socket.remote_endpoint().address().to_v4()));

	using Options = ServerListRequest::Options;
	if (request->options & Options::no_server_list || m_Game->queryPort() == 0xFFFF)
		co_return;

	if (request->options & Options::send_groups)
		co_return; // not yet implemented
	
	// a non-pushed server list is expected to contain the popular fields
	auto limit = std::min<std::uint32_t>(request->limitResultCount.value_or(500), 500);
	for (auto& server : co_await m_Game->GetServers(request->serverFilter, request->fieldList, limit)) {
		constexpr bool usePopularFields = true;
		co_await Send(ServerListRequest::GetServerBytes(*m_Game, server, request->fieldList, usePopularFields));
	}

	co_await Send(std::array<std::uint8_t, 5>{ 0x00, 0xFF, 0xFF, 0xFF, 0xFF });

	// configure adhoc updates
	if (request->options & Options::push_updates) {
		// the field-list contains the list of keys the client wants to receive for adhoc updates
		for (const auto& field : request->fieldList) {
			const auto& fieldStr = m_KeyListStorage.emplace_back(field);
			m_KeyList.emplace_back(fieldStr);
		}

		m_OnServerAdded = m_Game->OnServerAdded.connect([this](const Game::IncomingServer& server) {
			auto serverBytes = ServerListRequest::GetServerBytes(*m_Game, server, m_KeyList, false);
			std::vector<std::uint8_t> bytes;
			bytes.push_back(0x02); // PUSH_SERVER_MESSAGE
			bytes.push_back(static_cast<std::uint8_t>(serverBytes.size() >> 8));
			bytes.push_back(static_cast<std::uint8_t>(serverBytes.size()));
			bytes.append_range(serverBytes);
			m_SignalChannel.try_send(boost::system::error_code{}, bytes);
		});

		m_OnServerRemoved = m_Game->OnServerRemoved.connect([this](const std::string_view& ip, std::uint16_t port) {
			std::vector<std::uint8_t> bytes;
			bytes.push_back(0x04); // DELETE_SERVER_MESSAGE
			bytes.push_back(0);
			bytes.push_back(6); // 6 bytes for ip and port
			bytes.append_range(boost::asio::ip::make_address_v4(ip).to_bytes());
			bytes.append_range(std::array{
				static_cast<std::uint8_t>(port >> 8),
				static_cast<std::uint8_t>(port)
				});
			m_SignalChannel.try_send(boost::system::error_code{}, bytes);
		});
	}
}
