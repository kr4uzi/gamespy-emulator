#pragma once
#ifndef _GAMESPY_EMULATOR_H_
#define _GAMESPY_EMULATOR_H_
#include "asio.h"
#include "task.h"
#include <memory>

namespace gamespy
{
	class GameDB;
	class PlayerDB;
	class MasterServer;
	class LoginServer;
	class SearchServer;
	class BrowserServer;
	class CDKeyServer;
	class StatsServer;
	class AdminServer;
	class Emulator
	{
		boost::asio::io_context& m_Context;
		std::unique_ptr<GameDB> m_GameDB;
		std::unique_ptr<PlayerDB> m_PlayerDB;
		std::unique_ptr<MasterServer> m_MasterServer;
		std::unique_ptr<LoginServer> m_LoginServer;
		std::unique_ptr<SearchServer> m_SearchServer;
		std::unique_ptr<BrowserServer> m_BrowserServer;
		std::unique_ptr<CDKeyServer> m_CDKeyServer;
		std::unique_ptr<StatsServer> m_StatsServer;
		std::unique_ptr<AdminServer> m_AdminServer;

	public:
		Emulator(boost::asio::io_context& context);
		~Emulator();

		task<void> Launch(int argc, char* argv[]);

	private:
		task<void> InitGameDB(int argc, char* argv[]);
		task<void> InitPlayerDB(int argc, char* argv[]);
	};
}

#endif
