# ESPressMiner32 Micro-USB – Versionen / Changelog

Messwerte: ESP32-D0WDQ6 **Rev. 1.0**, 240 MHz, ESP32-DevKitC 38-Pin (Micro-USB, CP2102), eigener Stratum-Pool,
Software-Miner auf Core 0 an. Details je Schritt: [MESSPROTOKOLL.md](MESSPROTOKOLL.md).

## v1.1.0.008-pre4 (2026-10-16) – Vorabversion (Testing), ~1006 kH/s auf Rev. 1.0
Übernahme der Neuerungen aus dem USB-C-Projekt (1.3.0.011-pre1…pre4), **jeder Schritt einzeln gemessen**:
- **pre1 – Interrupts auf Core 1 je Hardware-Charge gesperrt** (`HW_IRQ_LOCK`): Hardware 969 → 971 kH/s.
  Der Errata-3.10-Schutz der Rev.-1-Schleifen (Stufe 5) bleibt voll wirksam.
- **pre2 – Hash-Schleifen per 4-KB-Ausrichtung festnageln** (`HW_LOOP_PIN`): **verworfen**. Die Ausrichtung verschiebt auf
  diesem Chip den gesamten IRAM-Code (−5 kH/s). Auf Rev. 1 zählt die absolute Lage. `HW_LOOP_PIN` bleibt als Schalter (0).
- **pre3 – schneller Software-Scan** auf Core 0 (vorberechnete Runden, früher Abbruch, beim Start gegen den
  Referenzpfad geprüft): 31 → 33 kH/s. Der Code liegt hier **im Flash statt im IRAM**, damit die Hash-Schleifen
  unverändert an ihrer gemessenen Stelle bleiben (IRAM byte-gleich mit 1.1.0.007).
- **pre4 – Fehler aus WLAN-Wellen getrennt angezeigt** (Messhilfe, in Arbeit): Dashboard z. B.
  `0 discarded (calculation errors) (+8 from 1 Wi-Fi wave)`, `/api/stats` zusätzlich `waveErrors` / `loopErrors`,
  `hwErrors` bleibt die Summe. Nichts wird gelöscht oder versteckt.
- Messung (je 1–4 min, Netzteil folgt im Langlauf): **~1006 kH/s** (Hardware 971,8 + Software 33,1), 0 Fehler.

## v1.1.0.007 (2026-10-14) – 1000 kH/s auf Rev. 1.0
- **Neue Schleife „O + DPORT read in LOAD wait +2“:** Das geschützte H7-Lesen (Errata 3.10: `rsil` →
  APB-Dummy-Lesen → `l16ui` → `wsr ps`) liegt jetzt in der Wartezeit nach LOAD, in der die SHA-Engine ohnehin
  rechnet, statt zwischen den Header-Writes. Spart 6 Takte: **246,9 statt 253 Takte/Hash**.
- Rückfallkette auf Chips vor Rev. 3: +2 → +4 → +6 → O_df → dfw (.004) → dfp → dfo → J_df → vorsichtiger.
  Schutzfenster unverändert.
- Messbetrieb (je 10 min, Laufzeit-Kontrolle aus): W 946,4 · O 946,4 · +6 957,7 · +4 957,8 · **+2 969,4** ·
  +0 969,4 kH/s Hardware – **0 von 52.403 Kandidaten verworfen**. +0 bringt keinen Takt mehr als +2, deshalb +2 (Reserve).
- Normalbetrieb: **Ø 1000,7 kH/s** (Hardware 969 + Software 31) über 3 h am Netzteil, 1 HW-Fehler auf 156.689 Kandidaten,
  235 Shares angenommen, 1 abgelehnt, 0 Neustarts.

## v1.1.0.006 (2026-10-14) – WLAN-Wächter und Schutzfenster, ~973 kH/s
- Codestand des USB-C-Projekts v1.3.0.009 übernommen:
  - **WLAN-Wächter:** nach 10 s ohne WLAN neu verbinden (Suche über alle Kanäle, z. B. nach Kanalwechsel im Mesh),
    nach 3 min offline Neustart. Vorher konnte der Miner nach einem Kanalwechsel dauerhaft offline bleiben.
  - **Schutzfenster:** Fehler-Schübe in festem Takt (USB-C-Board: alle ~600 s durch ein Gerät im Netz) werden
    erkannt; nach zwei Schüben rechnet der Miner einige Sekunden um den erwarteten Zeitpunkt mit einer
    vorsichtigeren Schleife. Auf Rev. 1 ist das „v2 +2 margin + DPORT protection“ (nie ungeschützt lesen).
  - Neue Felder in `/api/stats`: `channel`, `wifiDrops`, `wifiReconnects`, `wifiLastReason`, `guardActive`,
    `guardPeriodS`, `guardNextInS`, `guardBursts`, `guardWindows`, `guardErrors`.
- Hash-Schleife unverändert (dfw wie .004, 252 Takte), gemessen **~973,8 kH/s**, 1 von ~18.900 Kandidaten verworfen
  (Einzelfehler in Minute 3), 0 abgelehnte Shares. Auf ChatMini trat in den ersten 25 min kein Fehler-Schub auf.

## v1.1.0.005 (2026-10-14) – Weboberfläche und Konsole auf Englisch
- Dashboard, Einstellungen/Einrichtungs-Portal, Meldungen beim Speichern und Überhitzungsstatus komplett englisch
  (Settings, Blocks found, Save & restart, …), damit das Projekt auch international nutzbar ist.
- Serielle Konsole englisch (Selbsttest, Kalibrierung, Stratum, WLAN, Reset-Grund), ebenso die Schleifennamen
  in `/api/stats` (`hwLoop`, z. B. „J + DPORT protection, interleaved + wsr (rev. 1)“) und `resetReason` („power-on“ …).
- Hash-Code unverändert (gleiche Schleife wie .004, ~972 kH/s). Kommentare im Quelltext bleiben deutsch.

## v1.1.0.004 (2026-10-13) – Versuch `wsr ps` im Write-Abstand, ~972 kH/s, Nachtlauf bestanden
- Variante `hw_t_d_pl1_dfw`: auch `wsr ps` sitzt in einem Abstand zwischen den SHA-Writes. Ergebnis gleich .003
  (252 Takte, ~972 kH/s) – kein Gewinn, aber auch kein Nachteil.
- **Nachtlauf 11,1 h** (13./14.10.): Ø 973,2 kH/s, 0 Neustarts, 6 von 587.751 Kandidaten verworfen,
  Pool zählt 890 Shares ≈ 965 kH/s echte Arbeit, Chip 43–46 °C. Damit **stabil, Stand freigegeben**.
- `tools/miner_logger.py`: Langzeitprotokoll beliebiger Miner über `/api/stats` (log + csv).

## v1.1.0.003 (2026-10-13) – Schutzsequenz verschachtelt, ~972 kH/s
- Neue Variante „J + DPORT-Schutz, verschachtelt“: kein `nop` mehr vor dem geschützten Lesen (der APB-Zugriff
  sorgt selbst für den Abstand zwischen den SHA-Writes), `rsync` sitzt in der Pause nach dem nächsten Write.
- Gemessen: 252 statt 255 Takte/Hash, Benchmark 948,6 kH/s, Betrieb **~972 kH/s** (HW 942 + SW 31), 0 Rechenfehler.

## v1.1.0.002 (2026-10-13) – Schutzsequenz vorbereitet, ~962 kH/s
- Neue Variante „J + DPORT-Schutz, vorbereitet“: Interrupt-Sperre (`rsil`) und APB-Adresse werden schon in der
  Wartezeit nach dem LOAD vorbereitet, in der die CPU sonst nur `nop` ausführt. Dummy-Lesen und H7-Lesen bleiben
  direkt hintereinander (wie vom Errata-Workaround verlangt).
- Gemessen: 255 statt 259 Takte/Hash, Benchmark 937,6 kH/s, Betrieb **~962 kH/s** (HW 931 + SW 31), 0 Rechenfehler.

## v1.1.0.001 (2026-10-13) – schnelle Hash-Schleife jetzt auch auf Rev. 1.0, ~950 kH/s
- Stand des Hauptprojekts übernommen (v1.2.1 + eigene Weiterentwicklung „Master 004“):
  - Hash-Schleife **v2** (`src/hw_scan_v2.S`), portiert aus **NerdMiner Modded** (MIT, HenrysCat,
    https://github.com/HenrysCat/NerdMiner-Modded), Variante **„J“**: Header-Wörter 0..7 dauerhaft in Registern,
    Abstand 2 Takte zwischen den Register-Writes (Master 004, 240 Takte auf Rev. 3).
  - Auswahl beim Start per Known-Answer-Test (262 144 Nonces, 6 bekannte Treffer), Rückfallkette, Laufzeit-Kontrolle
    (≥ 8 von 256 Kandidaten unbestätigt → vorsichtigere Stufe).
  - Software-Miner auf Core 0 wieder an (+~31 kH/s).
  - Chip-Temperatur im Dashboard, Überhitzungsschutz (SW-Miner Pause ab 90 °C, Stopp ab 100 °C, weiter unter 80 °C).
  - Reset-Grund beim Start und in `/api/stats`, Projekt-Kennung jede Minute auf der Konsole.
- **Neu in dieser Variante:** Die v2-Schleifen lesen H7 direkt aus dem SHA-Puffer (DPORT). Auf Rev. 0/1 löst das
  den Errata-3.10-Fehler aus (`wifi assert`, Watchdog) – deshalb nimmt das Hauptprojekt dort die alte Schleife.
  Hier bekommen Rev.-0/1-Chips eigene Varianten `*_df`, die H7 mit der geschützten Sequenz lesen
  (Interrupts bis Level 5 sperren → Dummy-Lesen APB `0x3ff40078` → H7 lesen → Interrupts frei),
  nachgebaut nach `esp_dport_access_reg_read` aus ESP-IDF. Kette auf Rev. 1: J_df → schnell_df → +2_df → breit_df.
  Rev.-3-Chips laufen unverändert ohne Schutz.
- Gemessen: Benchmark **926,5 kH/s** HW (259 Takte/Hash), im Betrieb **~950 kH/s** (HW 920 + SW 31),
  0 Rechenfehler, Shares angenommen. Vorher (v1.0.0): 786 kH/s → **+21 %**.
- Partitionstabelle `huge_app` (3 MB App): Beim Umstieg von v1.0.0 **Bootloader + Partitionen mitflashen**
  (`pio run -t upload` macht das automatisch). Beim Testboard waren die gespeicherten Einstellungen danach weg
  → Einrichtung neu über das Portal.

## v1.0.0 (2026-10-01) – erste Micro-USB-Variante, 786 kH/s
- Abzweig vom Hauptprojekt (Stand vor v1.1.0): optionale LED an GPIO13, Errata-3.10-Workaround beim Lesen
  der SHA-Register (Revision wird beim Start erkannt), Kalibrierungs-Untergrenze 58 Takte auf alten Chips.
- Langzeittest 13 h 26 min am Pool hmpool.io: 786 kH/s konstant, Shares −1,3 % zur Erwartung.

## Quellen / Sources
- NerdMiner Modded (MIT) – https://github.com/HenrysCat/NerdMiner-Modded – Hash-Schleife v2
- NerdMiner_v2 (MIT) – https://github.com/BitMaker-hub/NerdMiner_v2 (Upstream von NerdMiner Modded)
- SparkMiner (MIT) – https://github.com/BitzyLabs/sparkminer – Idee gepufferter Register-Writes
- Espressif ESP32 Errata 3.10 / ESP-IDF (Apache-2.0) – geschützte DPORT-Lesesequenz (nachgebaut)
- Details und Lizenztexte: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
