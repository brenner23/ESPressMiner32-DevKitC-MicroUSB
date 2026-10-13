# Messprotokoll – ESPressMiner32 Micro-USB (ESP32-DevKitC 38-Pin, LED an GPIO13)

Testboard „ChatMini“: ESP32-D0WDQ6 **Rev. 1.0**, ESP32-DevKitC 38-Pin, CP2102.
Messungen mit Software-Miner an (Core 0), eigener Stratum-Pool im LAN (Diff 0,010).

| Version | Datum | Schleife | kH/s gesamt | HW / SW | HW-Fehler | Bemerkung |
|---|---|---|---|---|---|---|
| 1.0.0 | 2026-10-01 | klassisch + DPORT-Schutz | 786 | – | – | Langzeittest 13 h, Shares −1,3 % |
| 1.1.0.001 | 2026-10-13 | J + DPORT-Schutz | 951 | 920 / 31 | 0 / 5878 Kandidaten | Benchmark 926,5 kH/s, 259 Takte/Hash; 46 °C, 9/9 Shares |
| 1.1.0.002 | 2026-10-13 | J + DPORT-Schutz, vorbereitet | 962 | 931 / 31 | 0 / 1164 Kandidaten | Benchmark 937,6 kH/s, 255 Takte/Hash; 42 °C |
| 1.1.0.003 | 2026-10-13 | J + DPORT-Schutz, verschachtelt | **972** | 942 / 31 | 0 / 1773 Kandidaten | Benchmark 948,6 kH/s, 252 Takte/Hash; 43 °C |

## 1.1.0.003 – Schutzsequenz mit den Header-Writes verschachtelt
- Neue Variante `hw_t_d_pl1_dfp` (df = 3): kein `nop` nach dem Write von T[1] vor dem Lesen (der APB-Zugriff
  hält den Abstand zum nächsten Write ohnehin ein), `rsync` steht im Abstand nach dem Write von T[2] statt eines `nop`.
  Ablauf: `s32i T1` → `l32i APB` → `l16ui H7` → `wsr ps` → `s32i T2` → `rsync` → `s32i T3` …
- Ergebnis: 252 statt 255 Takte/Hash (3 gespart, erwartet 2), **+10 kH/s** (972 statt 962), 0 Fehler.
- Kette auf Rev. 1: dfp → dfo → J_df → schnell_df → +2_df → breit_df.
- Backup: `backups/ESPressMiner32_MicroUSB_v1.1.0.003_972khs.zip`.

## 1.1.0.002 – Schutzsequenz teilweise in die LOAD-Wartezeit
- Neue Variante `hw_t_d_pl1_dfo` (df = 2): `rsil a7, 5` und das Laden der APB-Adresse nach a6 passieren in der
  Wartezeit pl2 nach dem zweiten LOAD (Engine schreibt T[0..7], CPU wartet sonst mit nop; pl2 um 2 nop gekürzt).
  Beim Lesen bleiben Dummy-Lesen APB und H7 direkt hintereinander, `mov` entfällt (PS in a7, H7 direkt in a6).
- Ergebnis: 255 statt 259 Takte/Hash, **+11 kH/s** (962 statt 951), Known-Answer-Test beim ersten Versuch OK, 0 Fehler.
- Kette auf Rev. 1: dfo → J_df → schnell_df → +2_df → breit_df.
- Backup: `backups/ESPressMiner32_MicroUSB_v1.1.0.002_962khs.zip`.

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
