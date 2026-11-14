#pragma once
#ifndef  _GAMESPY_SAPPHIRE_H_
#define _GAMESPY_SAPPHIRE_H_

#include <string_view>
#include <stdexcept>
#include <ranges>
#include <concepts>
#include <cstddef>
#include <span>

// Gamespy uses a slightly modified Sapphire II stream cipher
// https://cryptography.org/mpj/sapphire.pdf
namespace gamespy {
    class sapphire {
    private:
        unsigned char cards[256];
        unsigned char rotor = 1;
        unsigned char ratchet = 3;
        unsigned char avalanche = 5;
        unsigned char last_plain = 7;
        unsigned char last_cipher = 11;
        void hash_init();

        unsigned char keyrand(int limit,
            const unsigned char* user_key,
            unsigned char keysize,
            unsigned char* rsum,
            unsigned* keypos);

    public:
        explicit sapphire(std::span<const unsigned char> key);
        ~sapphire();

        unsigned char decrypt(unsigned char b);
        unsigned char encrypt(unsigned char b);

        template<class R>
            requires std::ranges::range<R> && (
                std::same_as<std::ranges::range_value_t<R>, unsigned char>
                || std::same_as<std::ranges::range_value_t<R>, std::byte>
            )
        void encrypt(R&& range) {
            for (auto& c: range)
                c = static_cast<std::ranges::range_value_t<R>>(encrypt(static_cast<unsigned char>(c)));
        }

    private:
        void initialize(const unsigned char* key, unsigned char keysize);
        void hash_final(unsigned char* hash, unsigned char hashlength);
        void burn();
    };
}
#endif // ! _GAMESPY_SAPPHIRE_H_
