#include "utils.h"
#include <math.h>
#include <stdio.h>

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t hex_to_bytes(const char* hex, uint8_t* out, size_t maxLen)
{
    size_t n = 0;
    while (hex[0] && hex[1]) {
        if (n >= maxLen) return 0;
        int hi = hex_nibble(hex[0]);
        int lo = hex_nibble(hex[1]);
        if (hi < 0 || lo < 0) return 0;
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

void bytes_to_hex(const uint8_t* data, size_t len, char* out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i]     = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 0x0F];
    }
    out[2 * len] = '\0';
}

// Difficulty 1 Target = 0xFFFF * 2^208
static const double DIFF1 = 65535.0 * 4.1137614245742050e62;  // 2^208

double hash_difficulty(const uint8_t hash[32])
{
    double v = 0.0;
    for (int i = 31; i >= 0; i--) v = v * 256.0 + hash[i];
    if (v <= 0.0) return INFINITY;
    return DIFF1 / v;
}

double nbits_difficulty(uint32_t nbits)
{
    int exponent = nbits >> 24;
    double mantissa = nbits & 0x00FFFFFF;
    if (mantissa == 0) return 0.0;
    double target = mantissa * pow(256.0, exponent - 3);
    return DIFF1 / target;
}

void format_difficulty(double d, char* out, size_t outLen)
{
    static const char* units[] = { "", "K", "M", "G", "T", "P", "E" };
    if (d < 1.0) {
        snprintf(out, outLen, "%.6f", d);
        return;
    }
    int u = 0;
    while (d >= 1000.0 && u < 6) { d /= 1000.0; u++; }
    snprintf(out, outLen, "%.2f%s", d, units[u]);
}
