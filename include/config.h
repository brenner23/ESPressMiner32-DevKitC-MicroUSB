#pragma once
// ---------------------------------------------------------------------------
//  Allgemeine Einstellungen
// ---------------------------------------------------------------------------

#define MINER_NAME        "ESPressMiner32"
#define MINER_VERSION     "1.0.0"
#define MINER_REPO_URL    "https://github.com/brenner23/ESPressMiner32"

// Difficulty, die beim Pool angefragt wird (mining.suggest_difficulty).
// Bei ~1 MH/s ergibt 0.0005 etwa alle 8-9 Sekunden einen Share.
#define POOL_SUGGEST_DIFFICULTY  0.0005

// Pool-Verbindung gilt als tot, wenn so lange nichts empfangen wurde
#define STRATUM_RX_TIMEOUT_MS    (5UL * 60UL * 1000UL)
#define STRATUM_RECONNECT_MS     5000UL

// Statusausgabe auf der seriellen Konsole
#define STATS_INTERVAL_MS        5000UL

// Core-Zuordnung: Hardware-SHA-Miner exklusiv auf Core 1,
// WLAN/Stratum + Software-Miner auf Core 0
#define HW_MINER_CORE            1
#define SW_MINER_CORE            0
// Aus: Der SW-Miner bringt nur ~24 kH/s, bremst aber den HW-Miner ueber den
// gemeinsamen Bus um ~95 kH/s (gemessen: 486 kH/s mit, 557 kH/s ohne).
#define ENABLE_SW_MINER          0

// Nonce-Aufteilung zwischen Hardware- und Software-Miner
#define HW_NONCE_START           0x00000000UL
#define HW_NONCE_END             0xE0000000UL   // exklusiv
#define SW_NONCE_START           0xE0000000UL
#define SW_NONCE_END             0xFFFFFFFFUL

// Nonces pro Durchlauf, bevor auf einen neuen Job geprueft wird
#define HW_BATCH_SIZE            8192
#define SW_BATCH_SIZE            1024

// Sicherheitsabstand (CPU-Takte) auf die beim Start gemessenen Grenzwerte
// der Hardware-Pipeline. Kleiner = schneller, groesser = mehr Reserve.
#define HW_CAL_MARGIN_BLOCK      2
#define HW_CAL_MARGIN_LOAD       2

// mDNS-Name (Standard fuer den Hostnamen)  ->  http://espressminer.local
#define WEB_MDNS_NAME            "espressminer"

// Einrichtungs-Portal: WLAN "ESPressMiner32-XXXX" (min. 8 Zeichen Passwort)
#define PORTAL_AP_PASSWORD       "espressminer"
// So lange wird beim Start auf das gespeicherte WLAN gewartet, dann Portal zusaetzlich an
#define WIFI_CONNECT_TIMEOUT_MS  20000UL

// Optionale externe LED an GPIO13 (ESP32 DevKit USB-Micro, mit Vorwiderstand gegen GND):
// leuchtet, solange gemint wird; kurz aus bei jedem Share. -1 = aus
#define LED_PIN                  13
