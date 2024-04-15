#pragma once

#include <string>
#include <string_view>
#include <span>
#include <array>
#include <cstdint>
#include <expected>

namespace gamespy {
	namespace utils {
		std::string random_string(const std::string& table, std::string::size_type len);

		std::string encode(const std::string_view& passphrase, std::string message);
		std::string passencode(const std::string& password);
		std::string passdecode(std::string password);

		std::string generate_challenge(const std::string_view& name, const std::string_view& md5Password, const std::string_view& localChallenge, const std::string_view& remoteChallenge);

		void gs_xor(std::span<std::uint8_t>& message);

		std::string md5(const std::string_view& text);
	}
}