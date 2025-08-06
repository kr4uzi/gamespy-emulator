#pragma once
#ifndef _GAMESPY_UTILS_H_
#define _GAMESPY_UTILS_H_

#include <string>
#include <string_view>
#include <span>
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <chrono>

namespace gamespy {
	using Clock = std::chrono::system_clock;
	namespace utils {
		std::string random_string(const std::string& table, std::string::size_type len);

		std::string encode(const std::string_view& passphrase, std::string message);
		std::string passencode(const std::string& password);
		std::string passdecode(std::string password);

		std::string generate_challenge(const std::string_view& name, const std::string_view& md5Password, const std::string_view& localChallenge, const std::string_view& remoteChallenge);

		struct xor_types {
			static constexpr auto gamespy = std::array{ 'g', 'a', 'm', 'e', 's', 'p', 'y' };
			static constexpr auto GameSpy3D = std::array{ 'G', 'a', 'm', 'e', 'S', 'p', 'y', '3', 'D' };
			static constexpr auto ProjectAphex = std::array{ 'P', 'r', 'o', 'j', 'e', 'c', 't', 'A', 'p', 'h', 'e', 'x' };
			static constexpr auto Industries = std::array{ 'I', 'n', 'd', 'u', 's', 't', 'r', 'i', 'e', 's' };
		};

		template<typename R, std::size_t N> requires std::ranges::range<R>
		void gs_xor(R& message, const std::array<char, N>& gsxor = xor_types::gamespy)
		{
			std::size_t i = 0;
			for (auto& c : message)
				c ^= gsxor[i++ % gsxor.size()];
		}

		std::string md5(const std::string_view& text);

		template<typename T = std::string_view>
		std::optional<T> value_for_key(const std::span<const char>& textPacket, const std::string_view& key);

		template<>
		std::optional<std::string_view> value_for_key(const std::span<const char>& textPacket, const std::string_view& key);

		template<>
		std::optional<std::string> value_for_key(const std::span<const char>& textPacket, const std::string_view& key);

		template<>
		std::optional<std::uint32_t> value_for_key(const std::span<const char>& textPacket, const std::string_view& key);

		std::uint32_t to_date(const Clock::time_point& timepoint);
		Clock::time_point from_date(std::uint32_t gsDate);
	}
}

#endif
