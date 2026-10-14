# Messprotokoll – ESPressMiner32 Micro-USB (ESP32-DevKitC 38-Pin, LED an GPIO13)

Testboard „ChatMini“: ESP32-D0WDQ6 **Rev. 1.0**, ESP32-DevKitC 38-Pin, CP2102.
Messungen mit Software-Miner an (Core 0), eigener Stratum-Pool im LAN (Diff 0,010).

| Version | Datum | Schleife | kH/s gesamt | HW / SW | HW-Fehler | Bemerkung |
|---|---|---|---|---|---|---|
| 1.0.0 | 2026-10-01 | klassisch + DPORT-Schutz | 786 | – | – | Langzeittest 13 h, Shares −1,3 % |
| 1.1.0.001 | 2026-10-13 | J + DPORT-Schutz | 951 | 920 / 31 | 0 / 5878 Kandidaten | Benchmark 926,5 kH/s, 259 Takte/Hash; 46 °C, 9/9 Shares |
| 1.1.0.002 | 2026-10-13 | J + DPORT-Schutz, vorbereitet | 962 | 931 / 31 | 0 / 1164 Kandidaten | Benchmark 937,6 kH/s, 255 Takte/Hash; 42 °C |
| 1.1.0.003 | 2026-10-13 | J + DPORT-Schutz, verschachtelt | **972** | 942 / 31 | 0 / 1773 Kandidaten | Benchmark 948,6 kH/s, 252 Takte/Hash; 43 °C |
| 1.1.0.004 | 2026-10-13 | + wsr ps im Write-Abstand | 972 | 942 / 31 | 0 | Benchmark 948,8 kH/s, 252 Takte – **kein Gewinn** |
| 1.1.0.006 | 2026-10-14 | wie .004 + WLAN-Wächter, Schutzfenster | **973,8** | 942 / 32 | 1 / ~18.900 Kandidaten | Benchmark 948,7 kH/s, 252 Takte; 47 °C, 0 Fehler-Schübe |

## 1.1.0.006 – Stand USB-C v1.3.0.009 (WLAN-Wächter, Schutzfenster)
- Code des USB-C-Projekts v1.3.0.009 übernommen, Micro-USB-Teile (alle `_df`-Schleifen, LED GPIO13) behalten.
  Schutzfenster-Stufe auf Rev. 1: „v2 +2 margin + DPORT protection“ (die Rev.-3-Stufe würde ungeschützt lesen).
- Flashen stückweise (18 × 64 kB, ein Stück im 2. Versuch), `verify-flash` OK. Selbsttest OK, gewählt
  „J + DPORT protection, interleaved + wsr (rev. 1)“, Benchmark 948,7 kH/s (252 Takte/Hash).
- Lauf 14.10. ab 16:14, Logger alle 30 s: **973,7–975,4 kH/s, Ø 973,8**, 1 verworfener Kandidat nach ~3 min
  (Einzelfehler, kein Schub), danach 0. `guardBursts` 0, `wifiDrops` 0 – die ~600-s-Welle des USB-C-Boards
  zeigt sich auf ChatMini nicht (schon im Nachtlauf .004 nur 6 Fehler in 11 h). Pool: alle Shares angenommen.
- Screenshot `docs/dashboard_v1.1.0.006.png` (geschwärzt über Nur-Lese-Proxy).
- Backup: `backups/ESPressMiner32_MicroUSB_v1.1.0.006_973khs.zip`.

## 1.1.0.005 – nur Texte: Weboberfläche + Konsole englisch
- Keine Änderung am Hash-Code. Gebaut und die Webseite lokal mit Testdaten geprüft (Dashboard + Einrichtungsfenster);
  auf ChatMini noch nicht geflasht (Board war abgezogen).

## 1.1.0.004 – auch `wsr ps` in einen Write-Abstand (Versuch, kein Gewinn)
- Variante `hw_t_d_pl1_dfw` (df = 4): `s32i T1` → APB → H7 → `s32i T2` → `wsr ps` → `s32i T3` → `rsync` → `s32i T4`.
- Ergebnis: weiterhin 252 Takte/Hash, 972 kH/s. `wsr ps` kostet seine Zeit offenbar unabhängig von der Position
  (bzw. der folgende Write wartet darauf). → **.003 bleibt die Basis**, .004 nur dokumentiert.
- **Nachtlauf mit .004** (13.10. 21:46 – 14.10. 08:55, 11,1 h, Logger auf .49 alle 2 min): Ø 973,2 kH/s
  (971,8–974,5), 0 Neustarts, 6 verworfen von 587.751 HW-Kandidaten, Chip 43–46 °C, Thermik immer „normal“.
  Pool (shares.json, 22–09 Uhr): ChatMini 890 Shares = 965 kH/s; NerdMiner 959–999, ChatGPT (USB-C) 1039.
  → **.004 ist stabil und bleibt drauf.**

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
