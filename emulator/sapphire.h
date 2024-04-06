#pragma once
#ifndef  _GAMESPY_SAPPHIRE_H_
#define _GAMESPY_SAPPHIRE_H_

#include <string_view>
#include <stdexcept>
#include <ranges>

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
            unsigned char* user_key,
            unsigned char keysize,
            unsigned char* rsum,
            unsigned* keypos);

    public:
        template<class R>
            requires std::ranges::range<R>
        sapphire(R&& key)
            : sapphire(static_cast<unsigned char *>(std::data(key)), static_cast<std::uint8_t>(std::size(key)))
        {
            if (std::size(key) > 255)
                throw std::overflow_error("Key must not be longer than 255 characters!");
        }

        sapphire(unsigned char* key, unsigned char keysize);
        ~sapphire();

        unsigned char decrypt(unsigned char b);
        unsigned char encrypt(unsigned char b);

        template<class R>
            requires std::ranges::range<R>
        void encrypt(R&& range) {
            for (auto& c: range)
                c = encrypt(static_cast<unsigned char>(c));
        }

    private:
        void initialize(unsigned char* key, unsigned char keysize);
        void hash_final(unsigned char* hash, unsigned char hashlength);
        void burn();
    };
}
#endif // ! _GAMESPY_SAPPHIRE_H_
