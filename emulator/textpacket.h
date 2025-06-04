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

		std::string type;
		std::map<std::string, std::string> values;

		std::string str() const;
		std::string& operator[](const std::string& key);

		static std::expected<TextPacket, ParseError> parse(const std::span<const char>& buffer);
	};
}

