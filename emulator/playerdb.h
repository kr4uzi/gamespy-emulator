#pragma once
#ifndef _GAMESPY_PLAYER_DB_H_
#define _GAMESPY_PLAYER_DB_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <functional>
#include <optional>
#include "task.h"

namespace gamespy {
	struct PlayerData {
		std::uint64_t id = 0;
		std::string name;
		std::string email;
		std::string password;
		std::string country;

		std::uint64_t GetUserID() const;
		std::uint64_t GetProfileID() const;
		unsigned short session = 0;

		PlayerData() = default;
		PlayerData(const std::string_view& name, const std::string_view& email, const std::string_view& password, const std::string_view& country);
		PlayerData(std::uint64_t id, const std::string_view& name, const std::string_view& email, const std::string_view& password, const std::string_view& country);
	};

	class PlayerDB
	{
	public:
		PlayerDB();
		virtual ~PlayerDB();

		virtual task<void> Connect() = 0;
		virtual task<void> Disconnect() = 0;

		virtual task<bool> HasPlayer(const std::string_view& name) = 0;
		virtual task<std::optional<PlayerData>> GetPlayerByName(const std::string_view& name) = 0;
		virtual task<std::optional<PlayerData>> GetPlayerByPID(std::uint64_t pid) = 0;
		virtual task<std::vector<PlayerData>> GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) = 0;
		virtual task<void> CreatePlayer(PlayerData& data) = 0;
		virtual task<void> UpdatePlayer(const PlayerData& data) = 0;
	};
}
#endif