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
| 1.1.0.006 | 2026-10-14 | wie .004 + WLAN-Wächter, Schutzfenster | 973,8 | 942 / 32 | 1 / ~18.900 Kandidaten | Benchmark 948,7 kH/s, 252 Takte; 47 °C, 0 Fehler-Schübe |
| 1.1.0.007 | 2026-10-14 | O + H7-Lesen in LOAD-Wartezeit (+2) | **1000,7** | 969 / 31 | 0 / 52.403 (Messbetrieb), 1 / 156.689 (3 h) | Benchmark 971,8 kH/s, 246 Takte; 42–47 °C |
| ⭐ 1.1.0.008-pre1 | 2026-10-15 | wie .007 + Interrupt-Sperre Core 1 | **1002,5** | 971,2 / 31,1 | 0 / 3.697 Kandidaten (4 min) | aus USB-C Master 005 pre1; 36–44 °C, 5/0 Shares |
| 1.1.0.008-pre2 ✖ | 2026-10-15 | pre1 + HW-Schleifen festgenagelt (4 KB, 0xca0) | 996,4 | 966,1 / 30,3 | 0 / 3.228 (3 min) | **langsamer (−5 HW, −0,8 SW) → verworfen**, ganzer IRAM um 0xbfc verschoben |
| ⭐⭐ 1.1.0.008-pre3 | 2026-10-16 | pre1 + schneller SW-Scan im Flash | **1006,3** | 971,9 / 33,1 | 0 / 3.676 (4 min) | IRAM + alle 32 HW-Schleifen absolut wie pre1 |
| 1.1.0.008-pre4 | 2026-10-16 | pre3 + Wellen-Fehler getrennt (Messhilfe) | 1006,5 | 971,8 / 33,1 | 0 / 1.089 (1 min), Langlauf läuft | IRAM identisch mit pre3 |

## 1.1.0.008-pre4 – Wellen-Fehler getrennt anzeigen (Messhilfe, aus USB-C Master 005 Pre 4)
- `include/stats.h` `waveErrors`; `src/miner.cpp` guard::task zählt jeden HW-Fehler genau 1x als Welle, wenn er im
  Fenster (geschützt oder Stichprobe) oder in einem erkannten Schub liegt (`winInBurst` gegen Doppelzählung);
  `/api/stats` `waveErrors` + `loopErrors`; Dashboard „0 discarded (calculation errors) (+8 from 1 Wi-Fi wave)“;
  Konsole „HW errors (discarded): x (+y from z Wi-Fi waves)“. **Nichts wird gelöscht:** `hwErrors` bleibt die Summe.
- IRAM identisch mit pre3 (875 Symbole, gleiche Adressen). Flashen 18 × 64 kB ohne Wiederholung, NVS vorsorglich
  aus der Sicherung zurückgeschrieben (Wattebausch bei laufender Firmware = BOOT-Werksreset).
- Kurztest 1 min: 1006,5 kH/s (HW 971,8 + SW 33,1), 0 Fehler (Schleife 0 + Welle 0).
- Langlauf: Board am Netzteil, Logger im LAN fragt `/api/stats` alle 60 s ab (26 h).

## ⭐⭐ 1.1.0.008-pre3 – schneller SW-Scan, Code im Flash statt IRAM
- Ohne Festnageln (`HW_LOOP_PIN 0`). Schneller SW-Pfad aus USB-C (`src/sha256_sw.cpp`, `include/sha256_sw.h`,
  Selbsttest „Fast software scan == reference“ in `src/selftest.cpp`).
- Erster Versuch: `sw_h7` mit eigenem Abschnitt `.iram1.zswfast` → landete trotzdem **vor** den HW-Schleifen
  (Linker ordnet nicht nach Namen), 387 IRAM-Symbole verschoben → nicht geflasht.
- Lösung: `sw_h7` + `sw_scan` ohne IRAM_ATTR (Flash, Cache); `sw_scan_ref` (alter Pfad) bleibt im IRAM.
  Ergebnis: IRAM bis aufs Byte wie .007/pre1 (Ende 0x4009b0a8), alle 32 `hw_*` absolut gleich
  (aktiv `hw_t_o_dfr2` 0x4008e45c), `sw_h7` bei 0x400da6fc.
- Flashen: erster MAC-Lesversuch ohne Antwort (zu früh), zweiter OK; 18 × 64 kB, `verify-flash` OK.
  Einstellungen danach wieder leer → NVS (0x9000, 20 KB) aus der vorher gelesenen Flash-Sicherung (nur lokal, enthält WLAN-Daten)
  zurückgeschrieben, verifiziert.
- Messung 16.10. ab ~00:20: **HW 971,9 + SW 33,1 = 1006,3 kH/s** (pre1 1002,5) – SW +2 (USB-C mit IRAM: +5),
  HW unverändert, 0 Fehler / 3.676 Kandidaten nach 4 min, 7/0 Shares.
- Backup: `backups/ESPressMiner32_MicroUSB_v1.1.0.008-pre3_1006khs.zip`, Firmware `backups/firmware_v1.1.0.008-pre3.bin`.

## 1.1.0.008-pre2 ✖ – HW-Schleifen festnageln (verworfen)
- Änderung: in `src/hw_scan_v2.S` vor `hw_v2_fast` `.balign 4096` + `.skip 0xca0` (wie USB-C Master 005 Pre 3).
- Ergebnis: alle `hw_*` modulo 0x1000 wie .007, aber **absolut +0x2000** (aktive `hw_t_o_dfr2` 0x4008e45c → 0x4009045c).
  Ursache: die 4-KB-Ausrichtung gilt für die ganze Ausgabe-Section `.iram0.text` → deren Start springt von
  0x40080404 auf 0x40081000, **der gesamte IRAM-Code (auch SW-Scan, ISRs, WLAN) rutscht um 0xbfc**, die Schleifen
  danach durch das Auffüllen auf 0x2000. IRAM-Ende 0x4009b0a8 → 0x4009d0a8 (8 KB verschenkt).
- Messung 3 min (Portal-Neustart, Einstellungen vorher durch BOOT/Wattebausch beim Start gelöscht und neu eingetragen):
  **HW 966,1 + SW 30,3 = 996,4 kH/s**, 0 Fehler / 3.228 Kandidaten. Gegenüber pre1 −5 HW, −0,8 SW → verworfen.
- Lehre für Rev. 1: Nicht nur der Versatz in 4 KB zählt, sondern die absolute Lage (vermutlich IRAM-Block /
  Zugriffe beider Kerne). Festnageln per Ausrichtung verschiebt alles → hier ungeeignet.
- Merkpunkt: „BOOT 5 s = Werksreset“ verträgt sich nicht mit dem Wattebausch, wenn er beim Start noch drückt.

## ⭐ 1.1.0.008-pre1 – Interrupt-Sperre auf Core 1 (aus USB-C Master 005)
- Vorlage: `UEBERGABE_USB-C_Master005_2026-10-15.md`. Je Änderung ein Schritt (pre1 … pre4), wie auf USB-C.
- Vorher: kompletter Flash (4 MB) zweimal gelesen, identisch, lokal gesichert (backups/, nicht im Repo).
- Änderung: `HW_IRQ_LOCK 1` (`include/config.h`), in `mine_loop` nur für HW `XTOS_SET_INTLEVEL(XCHAL_EXCM_LEVEL)`
  vor `scan(...)`, `XTOS_RESTORE_INTLEVEL` danach. Die `_df`-Schleifen (Rev. 1) sichern den PS mit `rsil 5` und stellen
  ihn mit `wsr ps` wieder her → die Sperre bleibt während der Charge bestehen, der Errata-3.10-Schutz (Stufe 5) auch.
- Schleifen-Adressen: alle 32 `hw_*` exakt gleich wie .007 (`hw_v2_fast` 0x4008aca0).
- Flashen nur App, 18 × 64 kB mit 460800 Baud, alle im 1. Versuch, `verify-flash` OK.
- Messung 15.10. ~23:40, 4 min über `/api/stats`: **1002,5 kH/s (HW 971,2 + SW 31,1)**, HW +1,8 gegenüber .007
  (USB-C: +~2). **0 Fehler / 3.697 Kandidaten**, 5/0 Shares, 0 WLAN-Drops, Reset „power-on“, 36–44 °C.
- Backup: `backups/ESPressMiner32_MicroUSB_v1.1.0.008-pre1_1002khs.zip`, Firmware `backups/firmware_v1.1.0.008-pre1.bin`.

## 1.1.0.007 – H7-Lesen in der LOAD-Wartezeit, 1000 kH/s
- Idee: Nach dem LOAD wartet die Schleife ohnehin ~10 Takte (pl2) auf die SHA-Engine. Das geschützte H7-Lesen
  (`rsil 5` → APB-Dummy-Lesen `0x3ff40078` → `l16ui` → `wsr ps` → `rsync`) wird in diese Wartezeit gelegt
  (`df = 5`), `pre` nops davor, bis die Engine T[7] sicher geschrieben hat. Vier Stufen: pre = 6, 4, 2, 0.
- Messbetrieb `looptest` 14.10. 16:53–17:53, je 10 min, Laufzeit-Kontrolle aus:

  | Variante | HW kH/s | Takte | Kandidaten | verworfen |
  |---|---|---|---|---|
  | W: J + DPORT, verschachtelt + wsr (= .004/.006) | 946,4 | 253,0 | 8.699 | 0 |
  | O: O + DPORT, verschachtelt | 946,4 | 253,0 | 8.764 | 0 |
  | d: O + H7 in LOAD-Wartezeit +6 | 957,7 | 250,0 | 8.618 | 0 |
  | c: O + H7 in LOAD-Wartezeit +4 | 957,8 | 249,9 | 8.901 | 0 |
  | **b: O + H7 in LOAD-Wartezeit +2** | **969,4** | **246,9** | 8.886 | **0** |
  | a: O + H7 in LOAD-Wartezeit +0 | 969,4 | 246,9 | 8.735 | 0 |

  Auf der IP (mit Software-Miner): W/O ~977, d/c ~989, b/a ~1000,5 kH/s. a ist nicht schneller als b → b gewählt.
- Flashen stückweise (18 × 64 kB, Stück 2 und 3 erst im 3. bzw. 5. Versuch), `verify-flash` OK.
  Selbsttest OK, aktiv „O + DPORT read in LOAD wait +2 (rev. 1)“, Benchmark 971,8 kH/s (246 Takte/Hash).
- Normalbetrieb am Netzteil, Langlauf 14.10. 18:03–20:59 (Logger über WLAN alle 30 s, 352 Werte): **Ø 1000,7 kH/s**
  (998,5–1002,5), **1 HW-Fehler auf 156.689 Kandidaten** (20:58, nach fast 3 h), 235 Shares angenommen, 1 abgelehnt
  (19:10, kein HW-Fehler dabei), 0 Neustarts, 42–47 °C, RSSI −53 bis −66 dBm.
- Rechnerische Grenze auf Rev. 1: zum Rev.-3-Board (ohne Schutz, ~1024 kH/s) fehlen noch ~5 Takte.
- Screenshot `docs/dashboard_v1.1.0.007.png` (geschwärzt über Nur-Lese-Proxy).
- Backup: `backups/ESPressMiner32_MicroUSB_v1.1.0.007_1000khs.zip`.

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
