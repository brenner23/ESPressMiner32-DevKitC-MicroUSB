#pragma once
#include <stdint.h>

// Direkter Zugriff auf den SHA-256-Hardwarebeschleuniger des ESP32.
// Der Beschleuniger wird fuer die gesamte Laufzeit exklusiv reserviert;
// mbedTLS weicht dann automatisch auf Software-SHA aus.

void hw_sha_begin();

// Double-SHA256 eines 80-Byte-Headers ueber die Hardware (fuer Selbsttest).
void hw_sha256d_header(const uint8_t header[80], uint8_t out[32]);

// Scannt `count` Nonces ab `nonce`.
// blk1 = die ersten 16 Header-Woerter, blk2 = Header-Woerter 16..18 (Big-Endian-Werte).
uint32_t hw_scan(const uint32_t blk1[16], const uint32_t blk2[3],
                 uint32_t nonce, uint32_t count,
                 uint32_t* cands, int maxCands, int* nCands);

// Nur fuer den Selbsttest: liefert H7 jeder Nonce (gleicher Pipeline-Pfad wie hw_scan)
void hw_scan_h7(const uint32_t blk1[16], const uint32_t blk2[3],
                uint32_t nonce, uint32_t count, uint32_t* h7);

// Feste Wartezeiten der Pipeline in CPU-Takten, jeweils ab Absenden des Befehls:
//   block[0] = Block 1 (START), block[1] = Block 2 (CONTINUE), block[2] = Hash 2 (START)
//   load     = nach LOAD, bevor der naechste Befehl folgt
struct HwTiming {
    uint32_t block[3];
    uint32_t load;
};

void     hw_sha_set_timing(const HwTiming& t);
HwTiming hw_sha_timing();

// true auf ESP32 vor Revision 3 (DPORT-Errata-Workaround beim Lesen aktiv)
bool     hw_sha_dport_fix();
