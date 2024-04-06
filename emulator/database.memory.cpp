#include "database.memory.h"
#include "utils.h"
#include <print>
using namespace gamespy;

MemoryDB::MemoryDB(std::map<std::string, GameData> games)
	: Database(), m_Games(std::move(games))
{

}

MemoryDB::~MemoryDB()
{

}

bool MemoryDB::HasError() const noexcept { return !m_Error.empty(); }
std::string MemoryDB::GetError() const noexcept { return m_Error; }
void MemoryDB::ClearError() noexcept { m_Error.clear(); }

bool MemoryDB::HasGame(const std::string_view& name) noexcept
{
	return m_Games.find(std::string(name)) != m_Games.end();
}

GameData MemoryDB::GetGame(const std::string_view& name)
{
	auto iter = m_Games.find(std::string(name));
	if (iter != m_Games.end())
		return iter->second;

	throw std::exception("Game not found");
}

bool MemoryDB::HasPlayer(const std::string_view& name) noexcept
{
	auto iter = std::find_if(m_Players.begin(), m_Players.end(), [&](const auto& player) { return player.name == name; });
	return iter != m_Players.end();
}

PlayerData MemoryDB::GetPlayerByName(const std::string_view& name)
{
	auto iter = std::find_if(m_Players.begin(), m_Players.end(), [&](const auto& player) { return player.name == name; });
	if (iter != m_Players.end())
		return *iter;

	throw std::exception("Player not found!");
}

std::vector<PlayerData> MemoryDB::GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) noexcept
{
	std::vector<PlayerData> players;
	for (const auto& player : m_Players) {
		if (player.email == email && player.password == password) {
			players.push_back(player);
		}
	}

	return players;
}

void MemoryDB::CreatePlayer(PlayerData& data)
{
	data.id = static_cast<std::uint32_t>(m_Players.size() + 1);
	m_Players.push_back(data);
}

void MemoryDB::UpdatePlayer(const PlayerData& data)
{

}