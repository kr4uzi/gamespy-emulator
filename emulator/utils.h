#pragma once

#include <string_view>
#include <string>
#include <map>
#include <queue>
#include <span>
#include <array>
#include <cstdint>
#include <expected>

namespace gamespy {
	namespace utils {
		using namespace std::string_view_literals;

		struct TextPacket {
			static constexpr auto PACKET_END = R"(\final\)"sv;

			std::string type;
			std::map<std::string, std::string> values;

			std::string str() const;
			static std::queue<TextPacket> parse(const std::string& buffer);
		};

		std::string random_string(const std::string& table, std::string::size_type len);

		std::string encode(const std::string_view& passphrase, std::string message);
		std::string passencode(const std::string& password);
		std::string passdecode(std::string password);

		std::string generate_challenge(const std::string& name, const std::string& md5Password, const std::string& localChallenge, const std::string& remoteChallenge);

		void gs_xor(std::span<std::uint8_t>& message);

		std::string md5(const std::string_view& text);
	}
}