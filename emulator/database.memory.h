#pragma once
#include "database.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace gamespy {
	class MemoryDB : public Database
	{
		std::vector<PlayerData> m_Players;
		std::map<std::string, GameData> m_Games;
		std::string m_Error;

	public:
		MemoryDB(std::map<std::string, GameData> games = {});
		~MemoryDB();

		virtual bool HasError() const noexcept;
		virtual std::string GetError() const noexcept;
		virtual void ClearError() noexcept;

		virtual bool HasGame(const std::string_view& name) noexcept;
		virtual GameData GetGame(const std::string_view& name);

		virtual bool HasPlayer(const std::string_view& name) noexcept;
		virtual PlayerData GetPlayerByName(const std::string_view& name);
		virtual std::vector<PlayerData> GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) noexcept;
		virtual void CreatePlayer(PlayerData& data);
		virtual void UpdatePlayer(const PlayerData& data);
	};
}