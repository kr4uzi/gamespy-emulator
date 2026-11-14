#include "utils.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error{message};
}
}

int main()
{
	try {
		using namespace gamespy::utils;
		check(md5("") == "d41d8cd98f00b204e9800998ecf8427e", "empty MD5 vector");
		check(md5("abc") == "900150983cd24fb0d6963f7d28e17f72", "RFC 1321 MD5 vector");
		check(base64_encode("foobar") == "Zm9vYmFy", "Base64 encode vector");
		check(base64_decode("Zg==") == "f", "Base64 two padding bytes");
		check(base64_decode("Zm8=") == "fo", "Base64 one padding byte");
		check(base64_decode("YQAA") == std::string("a\0\0", 3), "preserve trailing zero bytes");
		std::string binary;
		for (unsigned int i = 0; i < 256; ++i) {
			binary += static_cast<char>(i);
			check(base64_decode(base64_encode(binary)) == binary, "binary Base64 round trip");
			check(passdecode(passencode(binary)) == binary, "binary GameSpy password round trip");
		}
		// GP/gpiUtility.c gpiEncodeString, using the SDK's GP_XOR_SEED.
		check(passencode("password") == "ZpOHgVy0nsg_", "SDK password vector");
		check(passencode("test-password") == "YpeHhgarjd8KeBGYVw__", "SDK long password vector");
		for (const auto* invalid : {"A", "====", "AA=A", "AAAA\n", "@@@="}) {
			bool rejected = false;
			try { base64_decode(invalid); } catch (const std::invalid_argument&) { rejected = true; }
			check(rejected, "reject malformed Base64");
		}
		check(parse_uint32("4294967295") == 4294967295u, "maximum uint32");
		for (const auto* invalid : {"", "-1", "4294967296", "2oops", " 4"})
			check(!parse_uint32(invalid), "reject invalid integer");
		check(!value_for_key<std::string_view>(std::span<const char>{}, ""), "empty packet key");
		bool rejected = false;
		try { encode("", "challenge"); } catch (const std::invalid_argument&) { rejected = true; }
		check(rejected, "empty challenge key");
		std::cout << "utility regressions passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
