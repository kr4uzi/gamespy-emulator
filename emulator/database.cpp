#include "database.h"
#include <format>
using namespace gamespy;

GameData::GameData(std::string name, std::string description, std::string secretKey, std::uint16_t queryPort, std::uint32_t gamestatsVersion, std::string gamestatsKey)
	: m_Name(std::move(name)), m_Description(std::move(description)), m_SecretKey(std::move(secretKey)), m_QueryPort(queryPort), m_GamestatsVersion(gamestatsVersion), m_GamestatsKey(std::move(gamestatsKey))
{

}

std::string gamespy::GameData::GetBrowserServer() const
{
	static constexpr auto PRIME = 0x9CCF9319; // same prime is also used to decode cd-keys

	auto hashcode = std::uint32_t{ 0 };
	for (const auto& c : m_Name)
		hashcode = hashcode * PRIME + std::tolower(c);

	static constexpr auto NUM_MASTER_SERVERS = 20;
	return std::format("{}.ms{}.gamespy.com", m_Name, hashcode % NUM_MASTER_SERVERS);
}

Database::Database()
{
	
}

Database::~Database()
{

}

PlayerData::PlayerData(const std::string_view& name, const std::string_view& email, const std::string_view& password, const std::string_view& country)
	: name(name), email(email), password(password), country(country)
{

}

PlayerData::PlayerData(std::uint32_t id, const std::string_view& name, const std::string_view& email, const std::string_view& password, const std::string_view& country)
	: id(id), name(name), email(email), password(password), country(country)
{

}

std::uint32_t PlayerData::GetUserID() const {
	if (id)
		return 40000000 - id;

	return 0;
}

std::uint32_t PlayerData::GetProfileID() const {
	if (id)
		return 50000000 - id;

	return 0;
}