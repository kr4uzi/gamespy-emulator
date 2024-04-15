#pragma once
#include <string_view>
#include <map>
#include <string>
#include <expected>
#include <span>

namespace gamespy {
	struct TextPacket {
		static constexpr auto PACKET_END = std::string_view{ R"(\final\)" };
		enum class ParseError {
			INCOMPLETE,
			INVALID
		};

		std::string_view type;
		std::map<std::string_view, std::string_view> values;

		std::string str() const;
		static std::expected<TextPacket, ParseError> parse(const std::span<const char>& buffer);
	};
}

