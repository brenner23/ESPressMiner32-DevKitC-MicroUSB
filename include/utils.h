#pragma once
#include <stdint.h>
#include <stddef.h>

static inline uint32_t read_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline void write_le32(uint8_t* p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

// Hex -> Bytes, liefert Anzahl geschriebener Bytes (0 bei Fehler)
size_t hex_to_bytes(const char* hex, uint8_t* out, size_t maxLen);
void   bytes_to_hex(const uint8_t* data, size_t len, char* out);

// Share-Difficulty eines Hashes (32 Byte, interne Byte-Reihenfolge)
double hash_difficulty(const uint8_t hash[32]);

// Netzwerk-Difficulty aus dem nBits-Feld
double nbits_difficulty(uint32_t nbits);

// Formatiert eine Difficulty kompakt (z.B. "1.23K", "0.00051")
void format_difficulty(double d, char* out, size_t outLen);
