#pragma once

#include <cstdint>
#include <string>
#include <iostream>

namespace helper{

    static inline uint32_t be32_to_host(uint32_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return static_cast<uint32_t>(__builtin_bswap32(x));
#else
        return static_cast<uint32_t>(x);
#endif
    }


    inline std::string uint_to_ascii(uint32_t word){
        std::string out;
        out.reserve(4);
        for (int shift = 24; shift >= 0; shift -= 8) {
            uint8_t byte = static_cast<uint8_t>((word >> shift) & 0xFFu);
            out.push_back(static_cast<char>(byte));
        }
        return out;
    }


    inline std::string uint_to_hex(uint32_t word){
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out(8, '0');
        for (int i = 7; i >= 0; --i) {
            out[i] = hex[word & 0xFu];
            word >>= 4;
        }
        return out;
    }


    inline std::string uint_to_bin(uint32_t word){
        std::string out(32, '0');
        for (int i = 31; i >= 0; --i) {
            out[i] = (word & 1u) ? '1' : '0';
            word >>= 1;
        }
        return out;
    }


    inline void print_word(uint32_t word){
        std::cout << "\n"
                  << " ASCII: " << uint_to_ascii(word) << "\n"
                  << "   Hex: 0x" << uint_to_hex(word) << "\n"
                  << "Binary: 0b" << uint_to_bin(word) << "\n\n";
    }


    inline int32_t to_int32(uint32_t word, bool reserve_bit)
    {
        if (reserve_bit) {
            word &= 0x7FFF'FFFFu;          // clear reserved bit 31
            if (word & 0x4000'0000u) {     // sign bit (bit 30)
                word |= 0x8000'0000u;      // sign-extend
            }
        }
        return static_cast<int32_t>(word);
    }


    inline int64_t to_int64(uint32_t hi, uint32_t lo, bool reserve_bit){
        uint64_t u;

        if (reserve_bit) {
            constexpr uint32_t MASK31 = 0x7FFF'FFFFu;   // keep bits 0..30
            hi &= MASK31;
            lo &= MASK31;

            // Pack into a 62-bit word: [hi(31 bits)][lo(31 bits)]
            u = (uint64_t(hi) << 31) | uint64_t(lo);

            // Sign-extend: sign bit is bit 61 (0-based)
            constexpr uint64_t SIGN62 = 1ULL << 61;
            if (u & SIGN62) {
                u |= (~0ULL) << 62; // set bits 62 and 63 to 1
            }
        } else {
            // Normal full 64-bit pack
            u = (uint64_t(hi) << 32) | uint64_t(lo);
        }

        return static_cast<int64_t>(u);
    }
} // end namespace helper
