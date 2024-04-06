#include "md5.h"

const std::array<std::uint32_t, 64> md5::k_array_ = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

const std::array<std::uint32_t, 64> md5::s_array_ = {
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
};

std::uint32_t md5::left_rotate(std::uint32_t x, std::uint32_t c) noexcept
{
    return (x << c) | (x >> (32 - c));
}

void md5::reset_m_array() noexcept
{
    m_array_first_ = m_array_.begin();
}

void md5::zeros_to_m_array(std::array<std::uint32_t, 16>::iterator m_array_last) noexcept
{
    for (; m_array_first_ != m_array_last; ++m_array_first_) {
        *m_array_first_ = 0;
    }
}

void md5::original_length_bits_to_m_array(std::uint64_t original_length_bits) noexcept
{
    *m_array_first_++ = (original_length_bits & 0xFFFFFFFF);
    *m_array_first_++ = (original_length_bits >> 32) & 0xFFFFFFFF;
}

void md5::hash_chunk() noexcept
{
    std::uint32_t A = a0_;
    std::uint32_t B = b0_;
    std::uint32_t C = c0_;
    std::uint32_t D = d0_;

    std::uint32_t F;
    unsigned int g;

    for (unsigned int i = 0; i < 64; ++i) {
        if (i < 16) {
            F = (B & C) | ((~B) & D);
            g = i;
        }
        else if (i < 32) {
            F = (D & B) | ((~D) & C);
            g = (5 * i + 1) & 0xf;
        }
        else if (i < 48) {
            F = B ^ C ^ D;
            g = (3 * i + 5) & 0xf;
        }
        else {
            F = C ^ (B | (~D));
            g = (7 * i) & 0xf;
        }

        std::uint32_t D_temp = D;
        D = C;
        C = B;
        B += left_rotate(A + F + k_array_[i] + m_array_[g], s_array_[i]);
        A = D_temp;
    }

    a0_ += A;
    b0_ += B;
    c0_ += C;
    d0_ += D;
}