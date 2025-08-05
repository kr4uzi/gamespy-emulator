#pragma once
#include "asio.h"

namespace gamespy {
	class GameDB;
	class PlayerDB;

	// gpcm = gamespy conneciton manager
	// the data model is actually (we only implemented basic battlefield 2 support):
	// User (email, password) <-> Profile (nickname) <-> Uniquenick (namespace)
	class LoginServer {
		static constexpr std::uint16_t PORT = 29900; // gpcm.gamespy.com
		boost::asio::ip::tcp::acceptor m_Acceptor;
		GameDB& m_GameDB;
		PlayerDB& m_PlayerDB;

	public:
		LoginServer(boost::asio::io_context& context, GameDB& gameDB, PlayerDB& playerDB);
		~LoginServer();

		boost::asio::awaitable<void> AcceptClients();

	private:
		boost::asio::awaitable<void> HandleIncoming(boost::asio::ip::tcp::socket socket);
	};
}