# Messprotokoll – ESPressMiner32 Micro-USB (ESP32-DevKitC 38-Pin, LED an GPIO13)

Testboard „ChatMini“: ESP32-D0WDQ6 **Rev. 1.0**, ESP32-DevKitC 38-Pin, CP2102.
Messungen mit Software-Miner an (Core 0), eigener Stratum-Pool im LAN (Diff 0,010).

| Version | Datum | Schleife | kH/s gesamt | HW / SW | HW-Fehler | Bemerkung |
|---|---|---|---|---|---|---|
| 1.0.0 | 2026-10-01 | klassisch + DPORT-Schutz | 786 | – | – | Langzeittest 13 h, Shares −1,3 % |
| 1.1.0.001 | 2026-10-13 | J + DPORT-Schutz | **951** | 920 / 31 | 0 / 651 Kandidaten | Benchmark 926,5 kH/s, 259 Takte/Hash; 41 °C |

## 1.1.0.001 – Stand Master 004 + v2-Schleife mit DPORT-Schutz
- Code aus `ESPressMiner32 - DevKit USBC Master 004` (v1.3.0.004) übernommen: v2-Schleife (NerdMiner Modded, MIT),
  T[3]-Nonce-Zählung, Kandidaten-Gegenprüfung, Chip-Temperatur + Überhitzungsschutz, Reset-Grund, Projekt-Kennung.
- Micro-USB-spezifisch: LED_PIN 13, `sha_rd` mit Errata-3.10-Schutz (wie v1.0.0), neue Schleifen `*_df` in
  `src/hw_scan_v2.S`: H7 wird mit `rsil 5` → Dummy-Lesen APB 0x3ff40078 → `l16ui` → `wsr ps` gelesen.
  Chips vor Rev. 3 bekommen nur diese Varianten (Kette J_df → schnell_df → +2_df → breit_df).
- Kosten des Schutzes: 259 statt ~241 Takte/Hash (J auf Rev. 3) – trotzdem +21 % gegenüber 1.0.0.
- Flashen: Partitionstabelle jetzt `huge_app` (vorher 1 MB App) → Bootloader + Partitionen + boot_app0 mitgeflasht,
  NVS war danach leer (Einstellungen neu über Portal).
- Board geht nicht selbst in den Download-Modus (GPIO0 über DTR wirkt nicht): BOOT dauerhaft halten
  (Wattebausch), Reset macht esptool per RTS. Bei Abrissen stückweise flashen (64 kB je Aufruf).
- Backup: `backups/ESPressMiner32_MicroUSB_v1.1.0.001_951khs.zip` (mit firmware/esp32dev/*.bin).
