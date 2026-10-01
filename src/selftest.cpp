#include "selftest.h"
#include "sha256_hw.h"
#include "sha256_sw.h"
#include "utils.h"
#include "config.h"

#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

// Bitcoin-Genesis-Block-Header
static const char* GENESIS_HEADER =
    "0100000000000000000000000000000000000000000000000000000000000000"
    "000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa"
    "4b1e5e4a29ab5f49ffff001d1dac2b7c";

// Hash in interner Byte-Reihenfolge
static const char* GENESIS_HASH =
    "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000";

static bool check(const char* name, bool ok)
{
    Serial.printf("  %-38s %s\n", name, ok ? "OK" : "FEHLER");
    return ok;
}

// Vergleicht die HW-Pipeline ueber `n` aufeinanderfolgende Nonces mit der Software.
// Liefert die Anzahl abweichender Hashes (-1 bei Speichermangel).
static int pipeline_mismatches(uint32_t n)
{
    uint32_t* h7 = (uint32_t*)malloc(n * sizeof(uint32_t));
    if (!h7) return -1;

    uint32_t b1[16], b2[3], mid[8];
    esp_fill_random(b1, sizeof(b1));
    esp_fill_random(b2, sizeof(b2));
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, b1);
    uint32_t start = esp_random();
    hw_scan_h7(b1, b2, start, n, h7);

    uint32_t w1[16] = { b2[0], b2[1], b2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    int mismatches = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t s[8];
        w1[3] = __builtin_bswap32(start + i);
        memcpy(s, mid, 32);
        sha256_transform(s, w1);
        memcpy(w2, s, 32);
        memcpy(s, SHA256_H0, 32);
        sha256_transform(s, w2);
        if (s[7] != h7[i]) mismatches++;
    }
    free(h7);
    return mismatches;
}

// Unabhaengig von den Wartezeiten tritt ~1 falscher Hash pro 25 000-50 000 Nonces auf
// (gemessen, vermutlich Busverkehr des anderen Kerns). Das kostet nur diesen einen
// Hash - Shares werden immer in Software nachgeprueft. Zu kurze Wartezeiten
// erzeugen dagegen Tausende Fehler; nur die sollen erkannt werden.
static const int CAL_MAX_ERRORS_6K  = 1;
static const int CAL_MAX_ERRORS_20K = 4;

static bool timing_ok(const HwTiming& tm)
{
    hw_sha_set_timing(tm);
    int errors = 0;
    for (int r = 0; r < 3; r++) {
        int e = pipeline_mismatches(2000);
        if (e < 0) return false;
        errors += e;
    }
    return errors <= CAL_MAX_ERRORS_6K;
}

// Sucht den kleinsten fehlerfreien Wert fuer `*field` (alle anderen Werte bleiben).
static bool find_min(HwTiming& tm, uint32_t* field, uint32_t from, uint32_t to, uint32_t step)
{
    for (uint32_t v = from; v <= to; v += step) {
        *field = v;
        if (timing_ok(tm)) return true;
    }
    return false;
}

bool selftest_calibrate()
{
    Serial.println("[Kalibrierung]");
    HwTiming tm = { { 120, 120, 120 }, 40 };     // sicher
    HwTiming lim;

    // Jede Phase einzeln: kleinsten Wert suchen, dann mit Reserve festhalten
    static const char* names[3] = { "Block 1", "Block 2", "Hash 2" };
    // Aeltere Chips: Die geschuetzte Leseroutine verzoegert das Auslesen und laesst
    // zu kurze Wartezeiten faelschlich fehlerfrei aussehen -> Untergrenze wie bei v3
    const uint32_t from = hw_sha_dport_fix() ? 58 : 30;
    for (int i = 0; i < 3; i++) {
        if (!find_min(tm, &tm.block[i], from, 120, 2)) {
            Serial.printf("  Keine fehlerfreie Wartezeit fuer %s gefunden\n", names[i]);
            return false;
        }
        lim.block[i] = tm.block[i];
        tm.block[i] += HW_CAL_MARGIN_BLOCK;
    }
    if (!find_min(tm, &tm.load, 0, 40, 1)) return false;
    lim.load = tm.load;
    tm.load += HW_CAL_MARGIN_LOAD;

    hw_sha_set_timing(tm);
    int errors = pipeline_mismatches(20000);
    bool ok = errors >= 0 && errors <= CAL_MAX_ERRORS_20K;
    Serial.printf("  Grenze: %lu/%lu/%lu Load %lu  ->  eingestellt: %lu/%lu/%lu Load %lu  (%d/20000 Fehler) %s\n",
                  (unsigned long)lim.block[0], (unsigned long)lim.block[1], (unsigned long)lim.block[2],
                  (unsigned long)lim.load, (unsigned long)tm.block[0], (unsigned long)tm.block[1],
                  (unsigned long)tm.block[2], (unsigned long)tm.load, errors, ok ? "OK" : "FEHLER");
    return ok;
}

bool selftest_run()
{
    Serial.println("[Selbsttest]");
    uint8_t header[80], expect[32], out[32];
    hex_to_bytes(GENESIS_HEADER, header, 80);
    hex_to_bytes(GENESIS_HASH, expect, 32);

    bool ok = true;

    sha256d(header, 80, out);
    ok &= check("Software SHA256d (Genesis)", memcmp(out, expect, 32) == 0);

    hw_sha256d_header(header, out);
    ok &= check("Hardware SHA256d (Genesis)", memcmp(out, expect, 32) == 0);

    // Midstate-Pfad des Software-Miners
    uint32_t b1[16], b2[3], mid[8];
    for (int i = 0; i < 16; i++) b1[i] = read_be32(header + 4 * i);
    for (int i = 0; i < 3; i++)  b2[i] = read_be32(header + 64 + 4 * i);
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, b1);
    uint32_t genesisNonce = 0x7c2bac1d;
    uint32_t cands[8];
    int nc = 0;
    sw_scan(mid, b2, genesisNonce - 2, 4, cands, 8, &nc);
    ok &= check("Software-Scan findet Genesis-Nonce", nc == 1 && cands[0] == genesisNonce);

    nc = 0;
    hw_scan(b1, b2, genesisNonce - 2, 4, cands, 8, &nc);
    ok &= check("Hardware-Scan findet Genesis-Nonce", nc == 1 && cands[0] == genesisNonce);

    // Zufaellige Header: HW == SW
    int mismatches = 0;
    for (int i = 0; i < 2000; i++) {
        uint8_t hdr[80], a[32], b[32];
        esp_fill_random(hdr, sizeof(hdr));
        sha256d(hdr, 80, a);
        hw_sha256d_header(hdr, b);
        if (memcmp(a, b, 32) != 0) mismatches++;
    }
    char label[64];
    snprintf(label, sizeof(label), "HW == SW (2000 Zufalls-Header, %d Fehler)", mismatches);
    ok &= check(label, mismatches <= 1);   // seltene Einzelfehler, siehe CAL_MAX_ERRORS_*

    // Pipeline ueber aufeinanderfolgende Nonces (wie im Mining-Betrieb)
    mismatches = pipeline_mismatches(5000);
    snprintf(label, sizeof(label), "HW-Pipeline 5000 Nonces (%d Fehler)", mismatches);
    ok &= check(label, mismatches >= 0 && mismatches <= 2);

    return ok;
}

void selftest_benchmark()
{
    uint32_t b1[16], b2[3], mid[8], cands[8];
    int nc;
    esp_fill_random(b1, sizeof(b1));
    esp_fill_random(b2, sizeof(b2));
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, b1);

    const uint32_t N_HW = 200000;
    uint32_t t0 = micros();
    hw_scan(b1, b2, 0, N_HW, cands, 8, &nc);
    uint32_t tHw = micros() - t0;

    const uint32_t N_SW = 10000;
    t0 = micros();
    sw_scan(mid, b2, 0, N_SW, cands, 8, &nc);
    uint32_t tSw = micros() - t0;

    Serial.printf("[Benchmark] Hardware: %.1f kH/s (%lu Takte/Hash) | Software: %.1f kH/s\n",
                  N_HW * 1000.0 / tHw, (unsigned long)((uint64_t)tHw * 240 / N_HW),
                  N_SW * 1000.0 / tSw);
}
