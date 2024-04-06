#pragma once
// all credits for this 64-bit safe MD5 implementation go to
// nectar_moon (original creator)
// Toby Speight (improvements)
// https://codereview.stackexchange.com/questions/163872/md5-implementation-in-c11

#include <limits>
#include <type_traits>
#include <array>
#include <iterator>
#include <cstdint>

class md5 {
private:
    std::uint32_t a0_ = 0x67452301;
    std::uint32_t b0_ = 0xefcdab89;
    std::uint32_t c0_ = 0x98badcfe;
    std::uint32_t d0_ = 0x10325476;

    std::array<std::uint32_t, 16> m_array_;
    std::array<std::uint32_t, 16>::iterator m_array_first_;

    static const std::array<std::uint32_t, 64> k_array_;
    static const std::array<std::uint32_t, 64> s_array_;

private:
    static std::uint32_t left_rotate(std::uint32_t x, std::uint32_t c) noexcept;

    template <class OutputIterator>
    static void uint32_to_byte(std::uint32_t n, OutputIterator& first) {

        *first++ = n & 0xff;
        *first++ = (n >> 8) & 0xff;
        *first++ = (n >> 16) & 0xff;
        *first++ = (n >> 24) & 0xff;
    }

    template <class OutputIterator>
    static void uint32_to_hex(std::uint32_t n, OutputIterator& out) {
        static auto const hex_chars = "0123456789abcdef";

        // print nibbles, low byte first (but high nibble before low nibble)
        // so shift is 4, 0, 12, 8, 20, 16, ...
        for (auto i = 0u; i < 32; i += 4) {
            *out++ = hex_chars[(n >> (i ^ 4)) & 0xf];
        }
    }

private:
    void reset_m_array() noexcept;

    template<class InputIterator>
    static std::uint8_t input_u8(const InputIterator& it)
    {
        return *it;
    }

    template<class InputIterator>
    void bytes_to_m_array(InputIterator& first,
        std::array<std::uint32_t, 16>::iterator m_array_last)
    {
        for (; m_array_first_ != m_array_last; ++m_array_first_) {
            *m_array_first_ = input_u8(first++);
            *m_array_first_ |= input_u8(first++) << 8;
            *m_array_first_ |= input_u8(first++) << 16;
            *m_array_first_ |= input_u8(first++) << 24;
        }
    }

    template<class InputIterator>
    void true_bit_to_m_array(InputIterator& first, std::ptrdiff_t chunk_length)
    {
        switch (chunk_length % 4) {
        case 0:
            *m_array_first_ = 0x00000080;
            break;
        case 1:
            *m_array_first_ = input_u8(first++);
            *m_array_first_ |= 0x00008000;
            break;
        case 2:
            *m_array_first_ = input_u8(first++);
            *m_array_first_ |= input_u8(first++) << 8;
            *m_array_first_ |= 0x00800000;
            break;
        case 3:
            *m_array_first_ = input_u8(first++);
            *m_array_first_ |= input_u8(first++) << 8;
            *m_array_first_ |= input_u8(first++) << 16;
            *m_array_first_ |= 0x80000000;
            break;
        }
        ++m_array_first_;
    }

    void zeros_to_m_array(std::array<std::uint32_t, 16>::iterator m_array_last) noexcept;
    void original_length_bits_to_m_array(std::uint64_t original_length_bits) noexcept;
    void hash_chunk() noexcept;

public:
    template<class InputIterator>
    typename std::enable_if<std::numeric_limits<typename InputIterator::value_type>::digits <= 8>::type
        update(InputIterator first, InputIterator last) {

        std::uint64_t original_length_bits = std::distance(first, last) * 8;

        std::ptrdiff_t chunk_length;
        while ((chunk_length = std::distance(first, last)) >= 64) {
            reset_m_array();
            bytes_to_m_array(first, m_array_.end());
            hash_chunk();
        }

        reset_m_array();
        bytes_to_m_array(first, m_array_.begin() + chunk_length / 4);
        true_bit_to_m_array(first, chunk_length);

        if (chunk_length >= 56) {
            zeros_to_m_array(m_array_.end());
            hash_chunk();

            reset_m_array();
            zeros_to_m_array(m_array_.end() - 2);
            original_length_bits_to_m_array(original_length_bits);
            hash_chunk();
        }
        else {
            zeros_to_m_array(m_array_.end() - 2);
            original_length_bits_to_m_array(original_length_bits);
            hash_chunk();
        }
    }

public:
    md5() = default;

    template<class InputIterator>
    md5(InputIterator first, InputIterator last)
    {
        update(first, last);
    }

    template<class Container>
    Container digest() const
    {
        Container c;
        digest(c);
        return c;
    }

    template<class Container>
    Container hex_digest() const
    {
        Container c;
        hex_digest(c);
        return c;
    }

    template <class Container>
    void digest(Container& container) const {
        container.resize(16);
        auto it = container.begin();

        uint32_to_byte(a0_, it);
        uint32_to_byte(b0_, it);
        uint32_to_byte(c0_, it);
        uint32_to_byte(d0_, it);
    }

    template <class Container>
    void hex_digest(Container& container) const {
        container.resize(32);
        auto it = container.begin();

        uint32_to_hex(a0_, it);
        uint32_to_hex(b0_, it);
        uint32_to_hex(c0_, it);
        uint32_to_hex(d0_, it);
    }
};