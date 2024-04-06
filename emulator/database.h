#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>

namespace gamespy {
	struct PlayerData {
		std::uint32_t id = 0;
		std::string name;
		std::string email;
		std::string password;
		std::string country;

		std::uint32_t GetUserID() const;
		std::uint32_t GetProfileID() const;
		unsigned short session = 0;

		PlayerData() = default;
		PlayerData(const std::string_view& name, const std::string_view& email, const std::string_view& password, const std::string_view& country);
		PlayerData(std::uint32_t id, const std::string_view& name, const std::string_view& email, const std::string_view& password, const std::string_view& country);
	};

	class GameData {
	public:
		enum class KeyType {
			STRING = 0,
			BYTE = 1,
			SHORT = 2
		};

	private:
		std::string m_Name;
		std::string m_Description;
		std::string m_SecretKey;
		std::uint16_t m_QueryPort;
		std::uint32_t m_GamestatsVersion;
		std::string m_GamestatsKey;

		// up to 254 most used values (implemented for completeness - was this used to save bandwith?)
		std::vector<std::string> m_PopularValues;
		
		// overrides how key-values are sent to clients (if a key is not present in this map, STRING will be used)
		std::map<std::string, KeyType> m_KeyTypeOverrides;

	public:
		GameData(std::string _name, std::string _description, std::string _secretKey, std::uint16_t _queryPort, std::uint32_t _gamestatsVersion, std::string _gamestatsKey);
		std::string GetBrowserServer() const; // calculates the designated master server (%s.ms%d.gamespy.com) for this game
	
		std::string_view GetName()          const { return m_Name; }
		std::string_view GetDescription()   const { return m_Description; }
		std::string_view GetSecretKey()     const { return m_SecretKey; }
		std::uint16_t GetQueryPort()        const { return m_QueryPort; }
		std::uint32_t GetGamestatsVersion() const { return m_GamestatsVersion; }
		std::string_view GetGamestatsKey()  const { return m_GamestatsKey; }

		KeyType GetKeyType(const std::string& key) const noexcept
		{
			const auto& keyTypeIter = m_KeyTypeOverrides.find(key);
			if (keyTypeIter != m_KeyTypeOverrides.end())
				return keyTypeIter->second;

			return KeyType::STRING;
		}

		void SetKeyTypeOverrides(decltype(m_KeyTypeOverrides) keyTypeOverrides) noexcept
		{
			m_KeyTypeOverrides = std::move(keyTypeOverrides);
		}

		// when sending server data via key-value pairs to the clients,
		// the list of popular-keys is first sent.
		// they are then used as a references to keys:
		// instead of sending the full key-string, only the index of the key within the popularKey array is sent
		// Note: No more than 254 keys must be stored (otherwise runtime overflow-exception will be thrown!)
		const std::vector<std::string>& GetPopularValues() const noexcept { return m_PopularValues; }

		template<typename R>
			requires std::ranges::range<R>
		void SetPopularValues(R&& popularValues)
		{
			if (std::ranges::size(popularValues) > 254)
				throw std::overflow_error("No more than 254 popular values are allowed!");

			m_PopularValues.clear();
			m_PopularValues.append_range(popularValues);
		}
	};

	class Database
	{
	public:
		Database();
		virtual ~Database();

		virtual bool HasError() const noexcept = 0;
		virtual std::string GetError() const noexcept = 0;
		virtual void ClearError() noexcept = 0;

		virtual bool HasGame(const std::string_view& name) noexcept = 0;
		virtual GameData GetGame(const std::string_view& name) = 0;

		virtual bool HasPlayer(const std::string_view& name) noexcept = 0;
		virtual PlayerData GetPlayerByName(const std::string_view& name) = 0;
		virtual std::vector<PlayerData> GetPlayerByMailAndPassword(const std::string_view& email, const std::string_view& password) noexcept = 0;
		virtual void CreatePlayer(PlayerData& data) = 0;
		virtual void UpdatePlayer(const PlayerData& data) = 0;
	};
}