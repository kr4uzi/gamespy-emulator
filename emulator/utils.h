#pragma once

#include <string>
#include <string_view>
#include <span>
#include <array>
#include <cstdint>
#include <expected>
#include <ranges>

namespace gamespy {
	namespace utils {
		std::string random_string(const std::string& table, std::string::size_type len);

		std::string encode(const std::string_view& passphrase, std::string message);
		std::string passencode(const std::string& password);
		std::string passdecode(std::string password);

		std::string generate_challenge(const std::string_view& name, const std::string_view& md5Password, const std::string_view& localChallenge, const std::string_view& remoteChallenge);

		struct xor_types {
			static constexpr auto gamespy = std::array{ 'g', 'a', 'm', 'e', 's', 'p', 'y' };
			static constexpr auto gamespy3d = std::array{ 'G', 'a', 'm', 'e', 'S', 'p', 'y', '3', 'D' };
		};

		template<typename R, std::size_t N> requires std::ranges::range<R>
		void gs_xor(R& message, const std::array<char, N>& gsxor = xor_types::gamespy)
		{
			std::size_t i = 0;
			for (auto& c : message)
				c ^= gsxor[i++ % gsxor.size()];
		}

		std::string md5(const std::string_view& text);
	}
}