#include "gamedb.h"
#include "game.h"
#include "bf2.h"
using namespace gamespy;

GameDB::GameDB()
{

}

GameDB::~GameDB()
{

}

GameDBInMemory::GameDBInMemory(boost::asio::io_context& context)
	: GameDB{ }, m_Context{ context }
{

}

GameDBInMemory::GameDBInMemory(boost::asio::io_context& context, nlohmann::json config)
	: GameDB{ }, m_Context{ context }, m_Config(std::move(config))
{
	if (!ValidateConfig(m_Config)) {
		throw std::runtime_error{ "invalid config" };
	}
}

GameDBInMemory::~GameDBInMemory()
{

}

task<void> GameDBInMemory::Connect()
{
	auto parse_keys = [](const nlohmann::json& keys, const nlohmann::json& sendAsShort, const nlohmann::json& sendAsByte, const nlohmann::json& saveAsReal, const nlohmann::json& ignoredKeys) {
		auto out = std::vector<GameData::GameKey>{};

		for (const auto& key : keys) {
			GameData::GameKey gk;
			gk.name = key;
			if (std::find(ignoredKeys.begin(), ignoredKeys.end(), key) != ignoredKeys.end()) {
				gk.send = GameData::GameKey::Send::as_string;
				gk.store = GameData::GameKey::Store::as_text;
			}
			else {
				if (std::find(sendAsShort.begin(), sendAsShort.end(), key) != sendAsShort.end())
					gk.send = GameData::GameKey::Send::as_short;
				else if (std::find(sendAsByte.begin(), sendAsByte.end(), key) != sendAsByte.end())
					gk.send = GameData::GameKey::Send::as_byte;

				if (std::find(saveAsReal.begin(), saveAsReal.end(), key) != saveAsReal.end())
					gk.store = GameData::GameKey::Store::as_real;
				else if (gk.send == GameData::GameKey::Send::as_short || gk.send == GameData::GameKey::Send::as_byte)
					gk.store = GameData::GameKey::Store::as_integer;
			}

			out.push_back(std::move(gk));
		}

		return out;
	};

	if (m_Config.empty()) {
		auto bf2 = std::make_shared<BF2>(m_Context);
		co_await bf2->Connect();
		m_Games.emplace("battlefield2", bf2);
	}
	else {
		for (const auto& entry : m_Config) {
			auto name = entry.at("name").get<std::string>();
			auto game = std::shared_ptr<Game>{};
			if (name == "battlefield2") {
				auto params = boost::mysql::connect_params{};
				params.server_address.emplace_host_and_port(entry.at("mysql-host").get<std::string>(), entry.at("mysql-port").get<std::uint16_t>());
				params.username = entry.at("mysql-username").get<std::string>();
				params.password = entry.at("mysql-password").get<std::string>();
				params.database = entry.at("mysql-database").get<std::string>();

				game = std::make_shared<BF2>(m_Context, params);
			}
			else {
				game = std::make_shared<Game>(GameData{
					.name = std::move(name),
					.secretKey = entry.at("secretKey").get<std::string>(),
					.keys = parse_keys(
						entry.at("keys"),
						entry.at("sendAsShort"),
						entry.at("sendAsByte"),
						entry.at("saveAsReal"),
						entry.at("ignoredKeys")
					),
					.misssingKeyPolicy = entry.at("autoKeys").get<bool>() ? GameData::MissingKeyPolicy::add_as_string : GameData::MissingKeyPolicy::ignore
				});
			}

			co_await game->Connect();
			m_Games.emplace(game->name(), game);
		}
	}

	if (!m_Games.contains("gmtest")) {
		auto gmtest = std::make_shared<Game>(GameData{
			.name = "gmtest",
			.secretKey = "HA6zkS",
			.keys = GameData::common_keys()
		});
		co_await gmtest->Connect();
		m_Games.emplace("gmtest", gmtest);
	}
}

task<void> GameDBInMemory::Disconnect()
{
	for (const auto& [name, game] : m_Games)
		co_await game->Disconnect();

	m_Games.clear();
}

task<bool> GameDBInMemory::HasGame(const std::string_view& name)
{
	co_return m_Games.contains(name);
}

task<std::shared_ptr<Game>> GameDBInMemory::GetGame(const std::string_view& name)
{
	co_return m_Games.at(name);
}

bool GameDBInMemory::ValidateConfig(const nlohmann::json& config)
{
	return true;
}
