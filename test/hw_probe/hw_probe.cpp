// Messprogramm fuer den SHA-Beschleuniger (Timing + Verhaltenstests)
// Bauen/Flashen:  pio run -e hw_probe -t upload
#include <Arduino.h>
#include <esp_random.h>
#include <soc/soc.h>
#include <soc/hwcrypto_reg.h>
#include <sha/sha_parallel_engine.h>
#include <xtensa/core-macros.h>
#include "soc/rtc.h"
#include "esp_timer.h"
#include "sha256_sw.h"

#define T         ((volatile uint32_t*)(SHA_TEXT_BASE))
#define START     (*(volatile uint32_t*)(SHA_256_START_REG))
#define CONT      (*(volatile uint32_t*)(SHA_256_CONTINUE_REG))
#define LOAD      (*(volatile uint32_t*)(SHA_256_LOAD_REG))
#define BUSY      (*(volatile uint32_t*)(SHA_256_BUSY_REG))
#define CC()      XTHAL_GET_CCOUNT()

static uint32_t blk[16], blk2[16], expect[8], expect2[8];

#define MINOF(label, setup, body) do { uint32_t best = 0xFFFFFFFF; \
    for (int r = 0; r < 200; r++) { setup; uint32_t a = CC(); body; uint32_t b = CC(); if (b - a < best) best = b - a; } \
    Serial.printf("%-28s min %lu Takte\n", label, best); } while (0)

static void IRAM_ATTR timing()
{
    volatile uint32_t x;
    MINOF("1 Write", , T[0] = blk[0]);
    MINOF("16 Writes", , for (int i = 0; i < 16; i++) T[i] = blk[i]);
    MINOF("1 BUSY-Read", , x = BUSY);
    MINOF("1 T-Read", , x = T[7]);
    MINOF("START..idle", for (int i = 0; i < 16; i++) T[i] = blk[i], START = 1; while (BUSY) {});
    MINOF("CONTINUE..idle", for (int i = 0; i < 16; i++) T[i] = blk[i], CONT = 1; while (BUSY) {});
    MINOF("LOAD..idle", START = 1; while (BUSY) {}, LOAD = 1; while (BUSY) {});
    MINOF("START+16 Writes..idle", for (int i = 0; i < 16; i++) T[i] = blk[i], START = 1; for (int i = 0; i < 16; i++) T[i] = blk2[i]; while (BUSY) {});
    MINOF("START+8 Writes..idle", for (int i = 0; i < 16; i++) T[i] = blk[i], START = 1; for (int i = 0; i < 8; i++) T[i] = blk2[i]; while (BUSY) {});
}

// --- Messungen ohne memw-Overhead (gepufferte Writes, Asm-Polling) ---
#define TB ((uint32_t*)(SHA_TEXT_BASE))
static inline __attribute__((always_inline)) void ctrl(uint32_t reg)
{
    __asm__ __volatile__("s32i.n %0, %1, 0\n memw" : : "r"(1), "r"(reg) : "memory");
}
static inline __attribute__((always_inline)) uint32_t rd(uint32_t a)
{
    uint32_t v;
    __asm__ __volatile__("l32i.n %0, %1, 0" : "=r"(v) : "r"(a) : "memory");
    return v;
}
static inline __attribute__((always_inline)) void fence() { __asm__ __volatile__("memw" : : : "memory"); }
static inline __attribute__((always_inline)) void idle() { while (rd(SHA_256_BUSY_REG)) {} }
static inline __attribute__((always_inline)) void spin(uint32_t n) { uint32_t s = CC(); while (CC() - s < n) {} }

static bool IRAM_ATTR block_ok_after(uint32_t n)
{
    for (int i = 0; i < 16; i++) TB[i] = blk[i];
    fence();
    ctrl(SHA_256_START_REG);
    spin(n);                       // KEIN Polling: feste Wartezeit
    ctrl(SHA_256_LOAD_REG);
    idle();
    for (int i = 0; i < 8; i++) if (rd(SHA_TEXT_BASE + 4 * i) != expect[i]) return false;
    return true;
}

static void IRAM_ATTR timing2()
{
    Serial.println("--- ohne memw ---");
    MINOF("16 gepufferte Writes+memw", , for (int i = 0; i < 16; i++) TB[i] = blk[i]; fence());
    MINOF("8 gepufferte Writes+memw", , for (int i = 0; i < 8; i++) TB[i] = blk[i]; fence());
    MINOF("1 BUSY-Read (asm)", , rd(SHA_256_BUSY_REG));
    MINOF("START..idle (asm)", for (int i = 0; i < 16; i++) TB[i] = blk[i]; fence(), ctrl(SHA_256_START_REG); idle());
    MINOF("LOAD..idle (asm)", ctrl(SHA_256_START_REG); idle(), ctrl(SHA_256_LOAD_REG); idle());
    MINOF("START+16 Writes..idle", for (int i = 0; i < 16; i++) TB[i] = blk[i]; fence(), ctrl(SHA_256_START_REG); for (int i = 0; i < 16; i++) TB[i] = blk2[i]; idle());

    // Echte Rechenzeit: kleinste feste Wartezeit, nach der LOAD das richtige Ergebnis liefert
    for (uint32_t n = 0; n <= 120; n += 2) {
        bool ok = true;
        for (int r = 0; r < 20 && ok; r++) ok = block_ok_after(n);
        if (ok) { Serial.printf("Block fertig nach fester Wartezeit: %lu Takte\n", n); break; }
    }
    // Wie lange dauert LOAD wirklich? (feste Wartezeit, dann T[7] lesen)
    for (uint32_t n = 0; n <= 60; n += 1) {
        bool ok = true;
        for (int r = 0; r < 20 && ok; r++) {
            for (int i = 0; i < 16; i++) TB[i] = blk[i];
            fence(); ctrl(SHA_256_START_REG); idle();
            ctrl(SHA_256_LOAD_REG); spin(n);
            ok = rd(SHA_TEXT_BASE + 28) == expect[7];
            idle();
        }
        if (ok) { Serial.printf("LOAD fertig nach: %lu Takten\n", n); break; }
    }
}

// --- Pipeline mit festen Wartezeiten statt BUSY-Polling ---
static uint32_t hb1[16], hb2[3];
static uint32_t refH7[2000];

static inline __attribute__((always_inline)) void until(uint32_t t0, uint32_t n) { while (CC() - t0 < n) {} }

static uint32_t IRAM_ATTR fixed_pipeline(uint32_t B, uint32_t L, uint32_t tail, uint32_t nonce, uint32_t count,
                                         uint32_t* h7, uint32_t* cycles)
{
    uint32_t* P = TB;
    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    fence();
    uint32_t c0 = CC();
    for (uint32_t k = 0; k < count; k++, nonce++) {
        P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
        P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
        ctrl(SHA_256_START_REG);
        uint32_t t = CC();
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = __builtin_bswap32(nonce);
        P[4] = 0x80000000; P[5] = 0; P[6] = 0; P[7] = 0;
        P[8] = 0; P[9] = 0; P[10] = 0; P[11] = 0; P[12] = 0; P[13] = 0; P[14] = 0; P[15] = 640;
        fence();
        until(t, B);
        ctrl(SHA_256_CONTINUE_REG);
        t = CC();
        until(t, tail);
        P[8] = 0x80000000; P[15] = 256;
        fence();
        until(t, B);
        ctrl(SHA_256_LOAD_REG);
        t = CC();
        until(t, L);
        ctrl(SHA_256_START_REG);
        t = CC();
        until(t, tail);
        for (int i = 8; i < 16; i++) P[i] = hb1[i];
        fence();
        until(t, B);
        ctrl(SHA_256_LOAD_REG);
        t = CC();
        until(t, L);
        h7[k] = rd(SHA_TEXT_BASE + 28);
    }
    *cycles = CC() - c0;
    return count;
}

// Phasen-Profil der Produktions-Pipeline (Basisregister, feste Wartezeiten 56/4)
static inline __attribute__((always_inline)) uint32_t* opq(uint32_t* p) { __asm__ __volatile__("" : "+r"(p)); return p; }
static inline __attribute__((always_inline)) uint32_t opv(uint32_t v) { __asm__ __volatile__("" : "+r"(v)); return v; }
#define CTRL(base, off) __asm__ __volatile__("s32i %0, %1, " #off "\n memw" : : "r"(1), "r"(base) : "memory")

#define CTRLN(base, off) __asm__ __volatile__("s32i %0, %1, " #off : : "r"(1), "r"(base) : "memory")

static void IRAM_ATTR profile()
{
    uint32_t* P = opq(TB);
    const uint32_t pad = opv(0x80000000), zero = opv(0);
    const uint32_t B1 = 60, B2 = 62, B3 = 62, L = 4, TAIL = 32;
    uint32_t acc[12] = {0};
    const int N = 1000;
    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    fence();
    for (int k = 0; k < N; k++) {
        uint32_t c[12];
        c[0] = CC();
        P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
        P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
        c[1] = CC();
        CTRLN(P, 0x90);
        uint32_t t = CC();
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = k;
        P[4] = pad; P[5] = zero; P[6] = zero; P[7] = zero;
        P[8] = zero; P[9] = zero; P[10] = zero; P[11] = zero; P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
        c[2] = CC();
        until(t, B1);
        c[3] = CC();
        CTRLN(P, 0x94);
        t = CC();
        until(t, TAIL);
        P[8] = pad; P[15] = 256;
        until(t, B2);
        c[4] = CC();
        CTRLN(P, 0x98);
        spin(L);
        CTRLN(P, 0x90);
        t = CC(); c[5] = t;
        until(t, TAIL);
        P[8] = hb1[8]; P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];
        P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
        c[6] = CC();
        until(t, B3);
        c[7] = CC();
        CTRLN(P, 0x98);
        spin(L);
        c[8] = CC();
        fence();
        c[9] = CC();
        volatile uint32_t v = P[7]; (void)v;
        c[10] = CC();
        for (int j = 1; j < 11; j++) acc[j] += c[j] - c[j - 1];
    }
    static const char* names[11] = { "", "8 Writes blk1", "START", "16 Writes blk2", "Rest bis B1",
        "CONTINUE..B2", "LOAD+L+START", "Tail+8 Writes", "Rest bis B3", "LOAD+L", "memw", };
    Serial.println("--- Phasenprofil ohne memw (Mittel, Takte) ---");
    uint32_t sum = 0;
    for (int j = 1; j < 11; j++) { Serial.printf("  %-24s %5lu\n", j == 10 ? "T[7] lesen" : names[j], acc[j] / N); sum += acc[j] / N; }
    Serial.printf("  %-24s %5lu\n", "SUMME", sum);
}

static void IRAM_ATTR tune()
{
    profile();
    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));
    uint32_t mid[8];
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, hb1);
    const uint32_t N = 2000, start = 12345;
    uint32_t w1[16] = { hb2[0], hb2[1], hb2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    for (uint32_t i = 0; i < N; i++) {
        uint32_t s[8];
        w1[3] = __builtin_bswap32(start + i);
        memcpy(s, mid, 32); sha256_transform(s, w1);
        memcpy(w2, s, 32); memcpy(s, SHA256_H0, 32); sha256_transform(s, w2);
        refH7[i] = s[7];
    }
    static uint32_t h7[2000];
    Serial.println("--- feste Wartezeiten: B=Block, L=Load, tail=Verzoegerung T[8..15] ---");
    for (uint32_t tail = 16; tail <= 32; tail += 8)
    for (uint32_t L = 0; L <= 8; L += 4)
    for (uint32_t B = 40; B <= 90; B += 2) {
        uint32_t cyc;
        fixed_pipeline(B, L, tail, start, N, h7, &cyc);
        int err = 0;
        for (uint32_t i = 0; i < N; i++) err += (h7[i] != refH7[i]);
        if (err == 0) {
            Serial.printf("tail=%2lu L=%lu  kleinstes B=%lu  -> %lu Takte/Hash = %.0f kH/s\n",
                          tail, L, B, cyc / N, 240000.0 / (cyc / (float)N));
            break;
        }
    }
}

// --- Test 1: Kosten von Schreibvorgaengen in Abhaengigkeit vom Zeitpunkt nach START ---
// Pro Offset d (Takte nach START, START mit memw abgeschlossen):
//   lat1 = 1 Write + memw   (bis der Wert in der Peripherie angekommen ist)
//   burst8 = 8 Writes + memw (Durchsatz)
// Zum Vergleich: dieselben Messungen ohne laufende Engine.
static void IRAM_ATTR measure_writes(bool engine, uint32_t d, uint32_t* minLat, uint32_t* avgLat,
                                     uint32_t* minBurst, uint32_t* avgBurst)
{
    uint32_t* P = opq(TB);
    const int R = 50;
    uint32_t sumL = 0, sumB = 0, mnL = ~0u, mnB = ~0u;
    for (int pass = 0; pass < 2; pass++) {
        for (int r = 0; r < R; r++) {
            for (int i = 0; i < 16; i++) P[i] = blk[i];
            fence();
            // Engine sicher im Leerlauf
            spin(200);
            if (engine) { ctrl(SHA_256_START_REG); }
            uint32_t t0 = CC();
            until(t0, d);
            uint32_t a = CC();
            if (pass == 0) {
                P[4] = 0x11111111;
                fence();
            } else {
                P[0] = 1; P[1] = 2; P[2] = 3; P[3] = 4; P[4] = 5; P[5] = 6; P[6] = 7; P[7] = 8;
                fence();
            }
            uint32_t c = CC() - a;
            if (pass == 0) { sumL += c; if (c < mnL) mnL = c; }
            else           { sumB += c; if (c < mnB) mnB = c; }
            spin(200);   // Engine fertig werden lassen
        }
    }
    *minLat = mnL; *avgLat = sumL / R; *minBurst = mnB; *avgBurst = sumB / R;
}

static void IRAM_ATTR write_cost_map()
{
    Serial.println("\n=== Test 1: Schreibkosten nach START (Takte) ===");
    uint32_t a, b, c, e;
    measure_writes(false, 0, &a, &b, &c, &e);
    Serial.printf("Leerlauf (keine Engine):   1 Write+memw min %3lu avg %3lu | 8 Writes+memw min %3lu avg %3lu\n",
                  a, b, c, e);
    Serial.println("Offset | 1 Write+memw min/avg | 8 Writes+memw min/avg");
    for (uint32_t d = 0; d <= 100; d += 5) {
        measure_writes(true, d, &a, &b, &c, &e);
        Serial.printf("  %3lu  |      %3lu / %3lu       |      %3lu / %3lu\n", d, a, b, c, e);
    }
    Serial.println("=== Ende Test 1 ===");
}

// --- Test 3: Schleifenende umbauen (T[7] lesen mit Block-1-Schreiben ueberlappen) ---
// Alle Varianten nutzen dieselben kalibrierten Wartezeiten wie die Produktion.
// Gemeinsame Phasen 1-2 (Block 1 -> Block 2 -> Hash 2), unterschiedliche Phase 3.
#define T3_B1 60
#define T3_B2 62
#define T3_B3 62
#define T3_L  4
#define T3_TAIL 32

// Variante 0 = Produktion: LOAD -> fence -> T[7] lesen -> (naechste Iteration schreibt T[0..7])
// Variante 1: letzte 8 Block-1-Woerter schon vor dem finalen LOAD? Nein - LOAD ueberschreibt T[0..7].
//   Stattdessen: nach LOAD T[0..6] mit Block1 schreiben, dann T[7] lesen, dann T[7] schreiben, START.
// Variante 2: wie 1, aber ohne fence - Ordnung nur ueber den Lesezugriff.
static uint32_t IRAM_ATTR pipe_variant(int variant, uint32_t nonce, uint32_t count, uint32_t* h7)
{
    uint32_t* P = opq(TB);
    const uint32_t pad = opv(0x80000000), zero = opv(0);
    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    fence();
    uint32_t c0 = CC();
    for (uint32_t k = 0; k < count; k++, nonce++) {
        // Block 1 erste Haelfte
        P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
        P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
        ctrl(SHA_256_START_REG);
        uint32_t t = CC();
        // Block 2
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = __builtin_bswap32(nonce);
        P[4] = pad; P[5] = zero; P[6] = zero; P[7] = zero;
        P[8] = zero; P[9] = zero; P[10] = zero; P[11] = zero; P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
        until(t, T3_B1);
        ctrl(SHA_256_CONTINUE_REG);
        t = CC();
        until(t, T3_TAIL);
        P[8] = pad; P[15] = 256;
        until(t, T3_B2);
        ctrl(SHA_256_LOAD_REG);       // Digest Hash 1 -> T[0..7]
        until(CC(), T3_L);
        ctrl(SHA_256_START_REG);      // Hash 2 startet
        t = CC();
        until(t, T3_TAIL);
        P[8] = hb1[8]; P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];
        P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
        until(t, T3_B3);
        ctrl(SHA_256_LOAD_REG);       // Digest Hash 2 -> T[0..7]
        until(CC(), T3_L);

        if (variant == 0) {
            fence();
            h7[k] = rd(SHA_TEXT_BASE + 28);
        } else if (variant == 1) {
            // Block-1-Woerter der naechsten Nonce vorbereiten, T[7] dazwischen lesen
            P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
            P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6];
            fence();
            h7[k] = rd(SHA_TEXT_BASE + 28);
            P[7] = hb1[7];
            ctrl(SHA_256_START_REG);
            // Rest der Schleife ueberspringt den Block-1-Schreibblock oben nicht -
            // fuer den Vergleich zaehlt nur die Taktzahl, H7 ist bereits gelesen.
            t = CC();
            P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = __builtin_bswap32(nonce + 1);
            P[4] = pad; P[5] = zero; P[6] = zero; P[7] = zero;
            P[8] = zero; P[9] = zero; P[10] = zero; P[11] = zero; P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
            until(t, T3_B1);
            ctrl(SHA_256_CONTINUE_REG);
            t = CC();
            until(t, T3_TAIL);
            P[8] = pad; P[15] = 256;
            until(t, T3_B2);
            ctrl(SHA_256_LOAD_REG);
            until(CC(), T3_L);
            ctrl(SHA_256_START_REG);
            t = CC();
            until(t, T3_TAIL);
            P[8] = hb1[8]; P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];
            P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
            until(t, T3_B3);
            ctrl(SHA_256_LOAD_REG);
            until(CC(), T3_L);
            fence();
            if (k + 1 < count) h7[k + 1] = rd(SHA_TEXT_BASE + 28);
            k++; nonce++;
        } else {
            // Variante 2: ohne fence, Ordnung nur ueber den Lesezugriff
            h7[k] = rd(SHA_TEXT_BASE + 28);
        }
    }
    return CC() - c0;
}

static void IRAM_ATTR test3()
{
    const uint32_t N = 20000, start = 424242;
    uint32_t* h7 = (uint32_t*)malloc(N * sizeof(uint32_t));
    uint32_t* ref = (uint32_t*)malloc(N * sizeof(uint32_t));
    if (!h7 || !ref) { Serial.println("kein Speicher"); return; }

    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));
    uint32_t mid[8];
    memcpy(mid, SHA256_H0, 32);
    sha256_transform(mid, hb1);
    uint32_t w1[16] = { hb2[0], hb2[1], hb2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    for (uint32_t i = 0; i < N; i++) {
        uint32_t s[8];
        w1[3] = __builtin_bswap32(start + i);
        memcpy(s, mid, 32); sha256_transform(s, w1);
        memcpy(w2, s, 32); memcpy(s, SHA256_H0, 32); sha256_transform(s, w2);
        ref[i] = s[7];
    }

    Serial.println("\n=== Test 3: Schleifenende (Wartezeiten 60/62/62 Load 4) ===");
    const char* names[3] = { "0 Produktion (fence, dann lesen)",
                             "1 Block1 schreiben, lesen ueberlappt",
                             "2 ohne fence (nur Lesezugriff ordnet)" };
    for (int v = 0; v < 3; v++) {
        uint32_t cyc = pipe_variant(v, start, N, h7);
        int err = 0;
        for (uint32_t i = 0; i < N; i++) err += (h7[i] != ref[i]);
        Serial.printf("  %-40s %lu Takte/Hash = %.0f kH/s | %d/%lu Fehler\n",
                      names[v], cyc / N, 240000.0 / (cyc / (float)N), err, (unsigned long)N);
    }
    Serial.println("=== Ende Test 3 ===");
    free(h7); free(ref);
}

// --- Test 4a: Wie stark wartet until()/spin() ueber das Ziel hinaus? ---
static void IRAM_ATTR wait_overshoot()
{
    Serial.println("\n=== Test 4a: Wartegenauigkeit ===");
    Serial.println("Ziel | until() ist-min | spin() ist-min  (Takte)");
    for (uint32_t goal = 0; goal <= 64; goal += 8) {
        uint32_t mu = ~0u, ms = ~0u;
        for (int r = 0; r < 200; r++) {
            uint32_t t = CC();
            until(t, goal);
            uint32_t u = CC() - t;
            if (u < mu) mu = u;
            uint32_t s0 = CC();
            spin(goal);
            uint32_t s = CC() - s0;
            if (s < ms) ms = s;
        }
        Serial.printf("  %2lu |       %2lu        |      %2lu\n", goal, mu, ms);
    }
    Serial.println("=== Ende Test 4a ===");
}

// --- Test 4b: 5 Warteschleifen pro Nonce durch exakte Verzoegerung ersetzen ---
// nop-Schlitten mit berechnetem Sprung: liefert n Takte ohne Taktzaehler-Abfrage.
static inline __attribute__((always_inline)) void delay_nops(uint32_t n)
{
    // Grobstufen zu je 4 Takten ueber Schleife, Rest per einzelnen nops entfaellt
    // (n ist im Betrieb konstant, der Compiler kann die Schleife gut planen)
    uint32_t i = n >> 2;
    while (i--) __asm__ __volatile__("nop; nop; nop; nop" ::: );
}

static uint32_t IRAM_ATTR pipe_nopwait(uint32_t nonce, uint32_t count, uint32_t* h7)
{
    uint32_t* P = opq(TB);
    const uint32_t pad = opv(0x80000000), zero = opv(0);
    // Bei nop-Wartung zaehlt die reine Befehlszeit; Zielwerte etwas hoeher waehlen,
    // weil die Schreibvorgaenge selbst schon Zeit kosten. Hier direkt gemessen.
    const uint32_t B1 = 60, B2 = 62, B3 = 62, L = 4, TAIL = 32;
    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    fence();
    uint32_t c0 = CC();
    for (uint32_t k = 0; k < count; k++, nonce++) {
        P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
        P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
        ctrl(SHA_256_START_REG);
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = __builtin_bswap32(nonce);
        P[4] = pad; P[5] = zero; P[6] = zero; P[7] = zero;
        P[8] = zero; P[9] = zero; P[10] = zero; P[11] = zero; P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
        delay_nops(B1 - 24);          // Schreibvorgaenge oben kosten schon ~24 Takte
        ctrl(SHA_256_CONTINUE_REG);
        delay_nops(TAIL - 4);
        P[8] = pad; P[15] = 256;
        delay_nops(B2 - TAIL);
        ctrl(SHA_256_LOAD_REG);
        delay_nops(L);
        ctrl(SHA_256_START_REG);
        delay_nops(TAIL - 4);
        P[8] = hb1[8]; P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];
        P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
        delay_nops(B3 - TAIL);
        ctrl(SHA_256_LOAD_REG);
        delay_nops(L);
        fence();
        h7[k] = rd(SHA_TEXT_BASE + 28);
    }
    return CC() - c0;
}

static void IRAM_ATTR test4()
{
    const uint32_t N = 20000, start = 555000;
    uint32_t* h7 = (uint32_t*)malloc(N * sizeof(uint32_t));
    uint32_t* ref = (uint32_t*)malloc(N * sizeof(uint32_t));
    if (!h7 || !ref) { Serial.println("kein Speicher"); return; }
    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));
    uint32_t mid[8]; memcpy(mid, SHA256_H0, 32); sha256_transform(mid, hb1);
    uint32_t w1[16] = { hb2[0], hb2[1], hb2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    for (uint32_t i = 0; i < N; i++) {
        uint32_t s[8]; w1[3] = __builtin_bswap32(start + i);
        memcpy(s, mid, 32); sha256_transform(s, w1);
        memcpy(w2, s, 32); memcpy(s, SHA256_H0, 32); sha256_transform(s, w2);
        ref[i] = s[7];
    }
    Serial.println("\n=== Test 4b: nop-Wartung statt Taktzaehler ===");
    uint32_t cyc = pipe_variant(0, start, N, h7);
    int err = 0; for (uint32_t i = 0; i < N; i++) err += (h7[i] != ref[i]);
    Serial.printf("  ccount-Wartung: %lu Takte/Hash = %.0f kH/s | %d Fehler\n",
                  cyc / N, 240000.0 / (cyc / (float)N), err);
    cyc = pipe_nopwait(start, N, h7);
    err = 0; for (uint32_t i = 0; i < N; i++) err += (h7[i] != ref[i]);
    Serial.printf("  nop-Wartung:    %lu Takte/Hash = %.0f kH/s | %d Fehler\n",
                  cyc / N, 240000.0 / (cyc / (float)N), err);
    Serial.println("=== Ende Test 4b ===");
    free(h7); free(ref);
}

// --- Test 5: Software-Pipeline. Naechsten Block 1 sofort nach dem Lesen starten,
// dann Kandidatenpruefung/Zaehler waehrend Block 1 rechnet (Engine steht nicht still). ---
static uint32_t IRAM_ATTR pipe_pipelined(uint32_t nonce, uint32_t count,
                                         uint32_t* cands, int* nCands)
{
    uint32_t* P = opq(TB);
    const uint32_t pad = opv(0x80000000), zero = opv(0);
    const uint32_t B1 = 60, B2 = 62, B3 = 62, L = 4, TAIL = 32;
    int found = 0;

    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    fence();
    // Block 1 der ersten Nonce starten
    P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
    P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
    ctrl(SHA_256_START_REG);
    uint32_t tStart = CC();

    uint32_t c0 = CC();
    for (uint32_t k = 0; k < count; k++, nonce++) {
        // Block 2 schreiben, waehrend Block 1 rechnet
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = __builtin_bswap32(nonce);
        P[4] = pad; P[5] = zero; P[6] = zero; P[7] = zero;
        P[8] = zero; P[9] = zero; P[10] = zero; P[11] = zero; P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
        until(tStart, B1);
        ctrl(SHA_256_CONTINUE_REG);
        uint32_t t = CC();
        until(t, TAIL);
        P[8] = pad; P[15] = 256;
        until(t, B2);
        ctrl(SHA_256_LOAD_REG);         // Hash-1-Digest -> T[0..7]
        until(CC(), L);
        ctrl(SHA_256_START_REG);        // Hash 2 startet
        t = CC();
        until(t, TAIL);
        P[8] = hb1[8]; P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];   // Block-1-Ende fuer naechste Nonce
        P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
        until(t, B3);
        ctrl(SHA_256_LOAD_REG);         // Hash-2-Ergebnis -> T[0..7]
        until(CC(), L);
        fence();
        uint32_t h7 = rd(SHA_TEXT_BASE + 28);

        // SOFORT Block 1 der naechsten Nonce starten
        P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
        P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
        ctrl(SHA_256_START_REG);
        tStart = CC();

        // Kandidatenpruefung waehrend Block 1 rechnet
        if ((h7 & 0xFFFF) == 0 && found < 8) cands[found++] = nonce;
    }
    *nCands = found;
    return CC() - c0;
}

// Referenzlauf gegen Software, misst zusaetzlich H7 aller Nonces
static uint32_t IRAM_ATTR pipe_pipelined_h7(uint32_t nonce, uint32_t count, uint32_t* h7out)
{
    uint32_t* P = opq(TB);
    const uint32_t pad = opv(0x80000000), zero = opv(0);
    const uint32_t B1 = 60, B2 = 62, B3 = 62, L = 4, TAIL = 32;
    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    fence();
    P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
    P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
    ctrl(SHA_256_START_REG);
    uint32_t tStart = CC();
    uint32_t c0 = CC();
    for (uint32_t k = 0; k < count; k++, nonce++) {
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = __builtin_bswap32(nonce);
        P[4] = pad; P[5] = zero; P[6] = zero; P[7] = zero;
        P[8] = zero; P[9] = zero; P[10] = zero; P[11] = zero; P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
        until(tStart, B1);
        ctrl(SHA_256_CONTINUE_REG);
        uint32_t t = CC();
        until(t, TAIL);
        P[8] = pad; P[15] = 256;
        until(t, B2);
        ctrl(SHA_256_LOAD_REG);
        until(CC(), L);
        ctrl(SHA_256_START_REG);
        t = CC();
        until(t, TAIL);
        P[8] = hb1[8]; P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];
        P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
        until(t, B3);
        ctrl(SHA_256_LOAD_REG);
        until(CC(), L);
        fence();
        uint32_t h7 = rd(SHA_TEXT_BASE + 28);
        P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
        P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
        ctrl(SHA_256_START_REG);
        tStart = CC();
        h7out[k] = h7;
    }
    return CC() - c0;
}

static void IRAM_ATTR test5()
{
    const uint32_t N = 20000, start = 777000;
    uint32_t* h7 = (uint32_t*)malloc(N * sizeof(uint32_t));
    uint32_t* ref = (uint32_t*)malloc(N * sizeof(uint32_t));
    if (!h7 || !ref) { Serial.println("kein Speicher"); return; }
    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));
    uint32_t mid[8]; memcpy(mid, SHA256_H0, 32); sha256_transform(mid, hb1);
    uint32_t w1[16] = { hb2[0], hb2[1], hb2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    for (uint32_t i = 0; i < N; i++) {
        uint32_t s[8]; w1[3] = __builtin_bswap32(start + i);
        memcpy(s, mid, 32); sha256_transform(s, w1);
        memcpy(w2, s, 32); memcpy(s, SHA256_H0, 32); sha256_transform(s, w2);
        ref[i] = s[7];
    }
    Serial.println("\n=== Test 5: Software-Pipeline (Block 1 vorziehen) ===");
    uint32_t cyc = pipe_variant(0, start, N, h7);
    int err = 0; for (uint32_t i = 0; i < N; i++) err += (h7[i] != ref[i]);
    Serial.printf("  bisher:         %lu Takte/Hash = %.0f kH/s | %d Fehler\n",
                  cyc / N, 240000.0 / (cyc / (float)N), err);
    cyc = pipe_pipelined_h7(start, N, h7);
    err = 0; for (uint32_t i = 0; i < N; i++) err += (h7[i] != ref[i]);
    Serial.printf("  Software-Pipe:  %lu Takte/Hash = %.0f kH/s | %d Fehler\n",
                  cyc / N, 240000.0 / (cyc / (float)N), err);
    Serial.println("=== Ende Test 5 ===");
    free(h7); free(ref);
}

// --- Test 6: Phasenprofil der Pipeline-Schleife (wo gehen die 265 Takte hin?) ---
static void IRAM_ATTR test6_profile()
{
    uint32_t* P = opq(TB);
    const uint32_t pad = opv(0x80000000), zero = opv(0);
    const uint32_t B1 = 62, B2 = 62, B3 = 62, L = 4, TAIL = 32;
    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));
    uint32_t acc[9] = {0};
    const int N = 2000;
    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    fence();
    P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
    P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
    ctrl(SHA_256_START_REG);
    uint32_t tStart = CC();
    for (int k = 0; k < N; k++) {
        uint32_t c[9]; c[0] = CC();
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = k;
        P[4] = pad; P[5] = zero; P[6] = zero; P[7] = zero;
        P[8] = zero; P[9] = zero; P[10] = zero; P[11] = zero; P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
        until(tStart, B1); c[1] = CC();          // Warten auf Block 1
        ctrl(SHA_256_CONTINUE_REG);
        uint32_t t = CC();
        until(t, TAIL); P[8] = pad; P[15] = 256;
        until(t, B2); c[2] = CC();               // Warten auf Block 2
        ctrl(SHA_256_LOAD_REG);
        until(CC(), L); c[3] = CC();             // LOAD 1
        ctrl(SHA_256_START_REG);
        t = CC();
        until(t, TAIL);
        P[8] = hb1[8]; P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];
        P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
        until(t, B3); c[4] = CC();               // Warten auf Hash 2
        ctrl(SHA_256_LOAD_REG);
        until(CC(), L); c[5] = CC();             // LOAD 2
        fence(); c[6] = CC();                    // memw
        volatile uint32_t h7 = P[7]; (void)h7; c[7] = CC();   // T[7] lesen
        P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
        P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
        ctrl(SHA_256_START_REG);
        tStart = CC(); c[8] = CC();              // Block 1 naechste Nonce + Start
        for (int j = 1; j < 9; j++) acc[j] += c[j] - c[j - 1];
    }
    static const char* nm[9] = { "", "Block2 schr.+Warten B1", "Pad+Warten B2", "LOAD1",
        "Start H2+Tail+Warten B3", "LOAD2", "memw", "T[7] lesen", "Block1+START naechste" };
    Serial.println("\n=== Test 6: Phasenprofil Pipeline (Takte) ===");
    uint32_t sum = 0;
    for (int j = 1; j < 9; j++) { Serial.printf("  %-26s %4lu\n", nm[j], acc[j] / N); sum += acc[j] / N; }
    Serial.printf("  %-26s %4lu\n", "SUMME", sum);
    Serial.println("=== Ende Test 6 ===");
}

// --- Test 7: Pipeline ohne memw vor dem Lesen ---
static uint32_t IRAM_ATTR pipe_nofence(uint32_t nonce, uint32_t count, uint32_t* h7out, bool useFence)
{
    uint32_t* P = opq(TB);
    const uint32_t pad = opv(0x80000000), zero = opv(0);
    const uint32_t B1 = 62, B2 = 62, B3 = 62, L = 4, TAIL = 32;
    for (int i = 8; i < 16; i++) P[i] = hb1[i];
    fence();
    P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
    P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
    ctrl(SHA_256_START_REG);
    uint32_t tStart = CC();
    uint32_t c0 = CC();
    for (uint32_t k = 0; k < count; k++, nonce++) {
        P[0] = hb2[0]; P[1] = hb2[1]; P[2] = hb2[2]; P[3] = __builtin_bswap32(nonce);
        P[4] = pad; P[5] = zero; P[6] = zero; P[7] = zero;
        P[8] = zero; P[9] = zero; P[10] = zero; P[11] = zero; P[12] = zero; P[13] = zero; P[14] = zero; P[15] = 640;
        until(tStart, B1);
        ctrl(SHA_256_CONTINUE_REG);
        uint32_t t = CC();
        until(t, TAIL);
        P[8] = pad; P[15] = 256;
        until(t, B2);
        ctrl(SHA_256_LOAD_REG);
        until(CC(), L);
        ctrl(SHA_256_START_REG);
        t = CC();
        until(t, TAIL);
        P[8] = hb1[8]; P[9] = hb1[9]; P[10] = hb1[10]; P[11] = hb1[11];
        P[12] = hb1[12]; P[13] = hb1[13]; P[14] = hb1[14]; P[15] = hb1[15];
        until(t, B3);
        ctrl(SHA_256_LOAD_REG);
        until(CC(), L);
        if (useFence) fence();
        uint32_t h7 = rd(SHA_TEXT_BASE + 28);
        P[0] = hb1[0]; P[1] = hb1[1]; P[2] = hb1[2]; P[3] = hb1[3];
        P[4] = hb1[4]; P[5] = hb1[5]; P[6] = hb1[6]; P[7] = hb1[7];
        ctrl(SHA_256_START_REG);
        tStart = CC();
        h7out[k] = h7;
    }
    return CC() - c0;
}

static void IRAM_ATTR test7()
{
    const uint32_t N = 20000, start = 999000;
    uint32_t* h7 = (uint32_t*)malloc(N * sizeof(uint32_t));
    uint32_t* ref = (uint32_t*)malloc(N * sizeof(uint32_t));
    if (!h7 || !ref) { Serial.println("kein Speicher"); return; }
    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));
    uint32_t mid[8]; memcpy(mid, SHA256_H0, 32); sha256_transform(mid, hb1);
    uint32_t w1[16] = { hb2[0], hb2[1], hb2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    for (uint32_t i = 0; i < N; i++) {
        uint32_t s[8]; w1[3] = __builtin_bswap32(start + i);
        memcpy(s, mid, 32); sha256_transform(s, w1);
        memcpy(w2, s, 32); memcpy(s, SHA256_H0, 32); sha256_transform(s, w2);
        ref[i] = s[7];
    }
    Serial.println("\n=== Test 7: Pipeline ohne memw vor dem Lesen ===");
    for (int f = 1; f >= 0; f--) {
        uint32_t cyc = pipe_nofence(start, N, h7, f);
        int err = 0; for (uint32_t i = 0; i < N; i++) err += (h7[i] != ref[i]);
        Serial.printf("  %s memw: %lu Takte/Hash = %.0f kH/s | %d Fehler\n",
                      f ? "mit " : "ohne", cyc / N, 240000.0 / (cyc / (float)N), err);
    }
    Serial.println("=== Ende Test 7 ===");
    free(h7); free(ref);
}

// --- Test 8: Skaliert der SHA-Chip mit dem CPU-Takt? (sicher, nur runtertakten) ---
static uint32_t IRAM_ATTR sha_block_cycles()
{
    // Reine Rechenzeit eines SHA-Blocks in CPU-Takten (per BUSY-Polling), Minimum aus 200
    uint32_t mn = ~0u;
    for (int r = 0; r < 200; r++) {
        for (int i = 0; i < 16; i++) TB[i] = blk[i];
        fence();
        uint32_t a = CC();
        ctrl(SHA_256_START_REG);
        while (rd(SHA_256_BUSY_REG)) {}
        uint32_t c = CC() - a;
        if (c < mn) mn = c;
    }
    return mn;
}

static void test8()
{
    esp_fill_random(blk, sizeof(blk));
    Serial.println("\n=== Test 8: SHA-Blockzeit bei verschiedenen CPU-Takten ===");
    const int freqs[] = { 240, 160, 80 };
    for (int f : freqs) {
        setCpuFrequencyMhz(f);
        delay(50);
        uint32_t cyc = sha_block_cycles();
        // Wandzeit in Nanosekunden = Takte / (MHz)
        Serial.printf("  %3d MHz: %3lu CPU-Takte pro Block = %.0f ns Wandzeit\n",
                      f, (unsigned long)cyc, 1000.0 * cyc / f);
    }
    setCpuFrequencyMhz(240);
    Serial.println("  Gleiche Takte -> SHA laeuft im CPU-Takt (Uebertakten wuerde helfen).");
    Serial.println("  Weniger Takte bei weniger MHz -> SHA hat festen Takt (Uebertakten bringt wenig).");
    Serial.println("=== Ende Test 8 ===");
}

// --- Test 9: Uebertaktung auf 320 MHz (Probe, danach sofort zurueck auf 240) ---
// Tatsaechliche CPU-Frequenz ueber den stabilen esp_timer messen (unabhaengig vom Takt).
static float IRAM_ATTR actual_mhz()
{
    int64_t t0 = esp_timer_get_time();
    uint32_t c0 = CC();
    while (esp_timer_get_time() - t0 < 20000) {}   // 20 ms echte Zeit
    uint32_t c1 = CC();
    int64_t t1 = esp_timer_get_time();
    return (float)(c1 - c0) / (float)(t1 - t0);
}

// Korrektheit: N Nonces ueber die Pipeline gegen Software pruefen (nutzt hb1/hb2)
static int IRAM_ATTR pipe_errors(uint32_t N, uint32_t* h7, uint32_t* ref)
{
    uint32_t mid[8]; memcpy(mid, SHA256_H0, 32); sha256_transform(mid, hb1);
    uint32_t w1[16] = { hb2[0], hb2[1], hb2[2], 0, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
    uint32_t w2[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
    for (uint32_t i = 0; i < N; i++) {
        uint32_t s[8]; w1[3] = __builtin_bswap32(555000 + i);
        memcpy(s, mid, 32); sha256_transform(s, w1);
        memcpy(w2, s, 32); memcpy(s, SHA256_H0, 32); sha256_transform(s, w2);
        ref[i] = s[7];
    }
    pipe_pipelined_h7(555000, N, h7);
    int err = 0; for (uint32_t i = 0; i < N; i++) err += (h7[i] != ref[i]);
    return err;
}

static void test9()
{
    const uint32_t N = 20000;
    uint32_t* h7 = (uint32_t*)malloc(N * sizeof(uint32_t));
    uint32_t* ref = (uint32_t*)malloc(N * sizeof(uint32_t));
    if (!h7 || !ref) { Serial.println("kein Speicher"); return; }
    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));

    // Baseline bei 240 - SOFORT ausgeben, damit es einen Absturz ueberlebt
    setCpuFrequencyMhz(240); delay(50);
    float mhz240 = actual_mhz();
    uint32_t blk240 = sha_block_cycles();
    int err240 = pipe_errors(N, h7, ref);
    Serial.println("\n=== Test 9: Uebertaktung ===");
    Serial.printf("  240 MHz: gemessen %.1f MHz | %lu Takte/Block | %d/%lu Fehler\n",
                  mhz240, (unsigned long)blk240, err240, (unsigned long)N);
    Serial.flush();

    // Zielfrequenzen der Reihe nach probieren (240 sicher als letzter Rueckfall)
    const int targets[] = { 320, 280, 260 };
    for (int tgt : targets) {
        Serial.printf("  Versuche %d MHz ...\n", tgt);
        Serial.flush();
        rtc_cpu_freq_config_t cfg;
        cfg.source = RTC_CPU_FREQ_SRC_PLL;
        cfg.source_freq_mhz = 320;          // 320-MHz-PLL
        cfg.div = (tgt == 320) ? 1 : 0;     // 320/1; andere Ziele s.u.
        cfg.freq_mhz = tgt;
        if (tgt == 320) { cfg.div = 1; cfg.freq_mhz = 320; }
        else { cfg.source_freq_mhz = 320; cfg.div = 1; cfg.freq_mhz = 320; }  // nur 320 versuchen
        rtc_clk_cpu_freq_set_config(&cfg);   // vollstaendige Funktion (schaltet PLL)
        delay(50);
        float mhzOC = actual_mhz();
        uint32_t blkOC = sha_block_cycles();
        int errOC = pipe_errors(N, h7, ref);
        setCpuFrequencyMhz(240); delay(50);   // sofort zurueck
        Serial.printf("  -> gemessen %.1f MHz | %lu Takte/Block | %d/%lu Fehler\n",
                      mhzOC, (unsigned long)blkOC, errOC, (unsigned long)N);
        Serial.flush();
        if (mhzOC > mhz240 + 10) {
            float faktor = mhzOC / mhz240;
            Serial.printf("  -> Takt real x%.2f. Erwartete Hashrate ~%.0f kH/s. Fehler: %s\n",
                          faktor, 902.0f * faktor, errOC == 0 ? "KEINE" : "VORHANDEN");
        }
        break;   // nur ein Versuch (320)
    }
    Serial.println("  CPU steht wieder auf 240 MHz.");
    Serial.println("=== Ende Test 9 ===");
    Serial.flush();
    free(h7); free(ref);
}

// --- Test 10: Kosten der Interrupts auf dem Rechenkern ---
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR test10()
{
    const uint32_t N = 20000;
    uint32_t* h7 = (uint32_t*)malloc(N * sizeof(uint32_t));
    if (!h7) { Serial.println("kein Speicher"); return; }
    esp_fill_random(hb1, sizeof(hb1));
    esp_fill_random(hb2, sizeof(hb2));

    Serial.println("\n=== Test 10: Interrupt-Kosten ===");
    // Mehrfach messen, damit Ausreisser durch Interrupts sichtbar werden
    uint32_t bestOn = ~0u, bestOff = ~0u, sumOn = 0, sumOff = 0;
    const int R = 8;
    for (int r = 0; r < R; r++) {
        uint32_t con = pipe_pipelined_h7(1000, N, h7);
        if (con < bestOn) bestOn = con; sumOn += con;

        portENTER_CRITICAL(&s_mux);          // alle Interrupts auf diesem Kern aus
        uint32_t coff = pipe_pipelined_h7(1000, N, h7);
        portEXIT_CRITICAL(&s_mux);
        if (coff < bestOff) bestOff = coff; sumOff += coff;
    }
    Serial.printf("  mit Interrupts:  best %lu / avg %lu Takte/Hash = best %.0f kH/s\n",
                  (unsigned long)(bestOn / N), (unsigned long)(sumOn / R / N), 240000.0 / (bestOn / (float)N));
    Serial.printf("  ohne Interrupts: best %lu / avg %lu Takte/Hash = best %.0f kH/s\n",
                  (unsigned long)(bestOff / N), (unsigned long)(sumOff / R / N), 240000.0 / (bestOff / (float)N));
    Serial.printf("  Unterschied im Durchschnitt: %ld Takte/Hash\n",
                  (long)(sumOn / R / N) - (long)(sumOff / R / N));
    Serial.println("=== Ende Test 10 ===");
    free(h7);
}

static void IRAM_ATTR run()
{
    test10();
    return;
    test9();
    test8();
    test7();
    test6_profile();
    test5();
    wait_overshoot();
    test4();
    test3();
    write_cost_map();
    timing();
    timing2();
    tune();
    uint32_t t0, t1;
    // 1) Schreibzeit 16 Register
    t0 = CC();
    for (int i = 0; i < 16; i++) T[i] = blk[i];
    t1 = CC();
    Serial.printf("16 Writes:          %lu Takte\n", t1 - t0);

    // 2) Lesezeit BUSY
    t0 = CC(); volatile uint32_t x = BUSY; x = BUSY; x = BUSY; x = BUSY; t1 = CC();
    Serial.printf("4 BUSY-Reads:       %lu Takte\n", t1 - t0);

    // 3) START -> idle
    for (int i = 0; i < 16; i++) T[i] = blk[i];
    t0 = CC(); START = 1; while (BUSY) {} t1 = CC();
    Serial.printf("START..idle:        %lu Takte\n", t1 - t0);
    t0 = CC(); LOAD = 1; while (BUSY) {} t1 = CC();
    Serial.printf("LOAD..idle:         %lu Takte\n", t1 - t0);
    bool ok = true;
    for (int i = 0; i < 8; i++) ok &= (T[i] == expect[i]);
    Serial.printf("Referenz-Hash:      %s\n", ok ? "OK" : "FALSCH");

    // 4) Latch-Test: nach START sofort Textregister ueberschreiben
    for (int i = 0; i < 16; i++) T[i] = blk[i];
    START = 1;
    for (int i = 0; i < 16; i++) T[i] = 0xDEADBEEF;
    while (BUSY) {}
    LOAD = 1; while (BUSY) {}
    ok = true;
    for (int i = 0; i < 8; i++) ok &= (T[i] == expect[i]);
    Serial.printf("Latch bei START:    %s\n", ok ? "JA (Ueberlappen moeglich)" : "NEIN");

    // 5) Wie viele Takte nach START darf man schreiben? (Suche Grenze)
    for (int d = 0; d <= 200; d += 4) {
        for (int i = 0; i < 16; i++) T[i] = blk[i];
        START = 1;
        uint32_t s = CC(); while (CC() - s < (uint32_t)d) {}
        T[0] = 0xDEADBEEF;
        while (BUSY) {}
        LOAD = 1; while (BUSY) {}
        bool good = true;
        for (int i = 0; i < 8; i++) good &= (T[i] == expect[i]);
        if (good) { Serial.printf("T[0] sicher ueberschreibbar nach %d Takten\n", d); break; }
    }

    // 6) Ohne BUSY-Warten nach LOAD lesen?
    for (int i = 0; i < 16; i++) T[i] = blk[i];
    START = 1; while (BUSY) {}
    LOAD = 1;
    uint32_t v7 = T[7];
    while (BUSY) {}
    Serial.printf("T[7] direkt nach LOAD: %s\n", v7 == expect[7] ? "gueltig" : "ungueltig");

    // 7) CONTINUE-Kette: bleibt Zustand erhalten, wenn danach T neu beschrieben wird?
    for (int i = 0; i < 16; i++) T[i] = blk[i];
    START = 1; while (BUSY) {}
    for (int i = 0; i < 16; i++) T[i] = blk2[i];
    t0 = CC(); CONT = 1; while (BUSY) {} t1 = CC();
    Serial.printf("CONTINUE..idle:     %lu Takte\n", t1 - t0);
    LOAD = 1; while (BUSY) {}
    ok = true;
    for (int i = 0; i < 8; i++) ok &= (T[i] == expect2[i]);
    Serial.printf("2-Block-Kette:      %s\n", ok ? "OK" : "FALSCH");

    // 8) Bleiben T[8..15] nach LOAD erhalten?
    for (int i = 0; i < 16; i++) T[i] = blk[i];
    START = 1; while (BUSY) {}
    LOAD = 1; while (BUSY) {}
    ok = true;
    for (int i = 8; i < 16; i++) ok &= (T[i] == blk[i]);
    Serial.printf("T[8..15] nach LOAD erhalten: %s\n", ok ? "JA" : "NEIN");

    // 9) Bleibt T nach START (ohne LOAD) erhalten?
    for (int i = 0; i < 16; i++) T[i] = blk[i];
    START = 1; while (BUSY) {}
    ok = true;
    for (int i = 0; i < 16; i++) ok &= (T[i] == blk[i]);
    Serial.printf("T[0..15] nach START erhalten: %s\n", ok ? "JA" : "NEIN");
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    setCpuFrequencyMhz(240);
    esp_sha_try_lock_engine(SHA1);
    esp_sha_try_lock_engine(SHA2_256);

    esp_fill_random(blk, sizeof(blk));
    esp_fill_random(blk2, sizeof(blk2));
    memcpy(expect, SHA256_H0, 32);
    sha256_transform(expect, blk);
    memcpy(expect2, expect, 32);
    sha256_transform(expect2, blk2);
    Serial.println("\n=== SHA-HW-Probe ===");
    run();
    Serial.println("=== Ende ===");
}

void loop() { delay(1000); }
