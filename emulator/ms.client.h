#pragma once
#ifndef _GAMESPY_BROWSER_CLIENT_H_
#define _GAMESPY_BROWSER_CLIENT_H_

#include "asio.h"
#include "sapphire.h"
#include "sb_request.h"
#include <boost/asio/experimental/channel.hpp>
#include <boost/signals2.hpp>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace gamespy {
	class GameDB;
	class Game;
	class BrowserClient {
		boost::asio::ip::tcp::socket m_Socket;
		GameDB& m_DB;

		std::optional<sapphire> m_Cypher;
		std::shared_ptr<Game> m_Game;
		std::vector<std::string> m_KeyListStorage;
		std::vector<std::string_view> m_KeyList; // points to m_KeyListStorage
		std::vector<std::uint8_t> m_OutBuffer;
		boost::signals2::scoped_connection m_OnServerAdded, m_OnServerRemoved;
		boost::asio::experimental::channel<void(boost::system::error_code, std::vector<std::uint8_t>)> m_SignalChannel;

	public:
		BrowserClient(BrowserClient&& rhs) = default;
		BrowserClient& operator=(BrowserClient&& rhs) = default;

		BrowserClient(boost::asio::ip::tcp::socket socket, GameDB &db);
		~BrowserClient();

		boost::asio::awaitable<void> Process();

	private:
		boost::asio::awaitable<void> StartEncryption(const decltype(ServerListRequest::challenge)& clientChallenge, const Game& game);
		boost::asio::awaitable<void> HandleServerListRequest(const std::span<const std::uint8_t>& bytes);

		template<class R> requires std::ranges::range<R> && std::same_as<std::ranges::range_value_t<R>, unsigned char>
		boost::asio::awaitable<void> Send(R&& bytes)
		{
			m_Cypher->encrypt(bytes);
			co_await m_Socket.async_send(boost::asio::buffer(bytes), boost::asio::use_awaitable);
		}
	};
}

#endif
