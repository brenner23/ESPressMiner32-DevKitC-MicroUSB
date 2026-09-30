#pragma once

// Konfiguration ueber die serielle Konsole (115200 Baud), eine JSON-Zeile pro Befehl:
//
//   {"cmd":"info"}                       Version, MAC, IP, konfiguriert?
//   {"cmd":"get"}                        aktuelle Einstellungen (ohne WLAN-Passwort)
//   {"cmd":"set", ...Felder wie /api/config...}   pruefen, speichern, Neustart
//   {"cmd":"reset"}                      Einstellungen loeschen, Neustart ins Portal
//   {"cmd":"restart"}                    Neustart
//
// Jede Antwort ist eine Zeile, die mit "@CM " beginnt, gefolgt von JSON.
// Ausserdem: BOOT-Taste (GPIO0) 5 s gedrueckt halten = Einstellungen loeschen.
void serial_config_start();
