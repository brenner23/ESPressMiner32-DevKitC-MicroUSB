#pragma once

// Kleiner HTTP-Server (Core 0): Dashboard unter "/" und JSON unter "/api/stats"
// bzw. "/api/config". Blockiert nie den Hardware-Miner auf Core 1.
void web_task(void* arg);

// Einrichtungs-Portal: eigenes WLAN "ESPressMiner32-XXXX", Seite unter 192.168.4.1.
// keepSta = true: parallel weiter mit dem gespeicherten WLAN verbinden; sobald das
// klappt, wird das Portal automatisch wieder abgeschaltet.
void web_start_portal(bool keepSta);
bool web_portal_active();
