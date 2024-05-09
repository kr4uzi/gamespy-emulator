#include "playerdb.h"
using namespace gamespy;

PlayerDB::PlayerDB()
{
	
}

PlayerDB::~PlayerDB()
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
	return id;
}

std::uint32_t PlayerData::GetProfileID() const {
	return id;
}