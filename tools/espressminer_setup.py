#!/usr/bin/env python3
"""
ESPressMiner32 Setup - Zugangsdaten ueber die serielle Schnittstelle einstellen.

Beispiele:
  python espressminer_setup.py                  # interaktiv, COM-Port wird gesucht
  python espressminer_setup.py --port COM22 --get
  python espressminer_setup.py --ssid MeinWLAN --password geheim \
         --pool stratum+tcp://pool.example.com:3333 --wallet ADRESSE.Worker
  python espressminer_setup.py --reset          # Einstellungen loeschen (-> Portal)

Benoetigt pyserial:  pip install pyserial
"""

import argparse
import getpass
import json
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial fehlt - bitte installieren:  pip install pyserial")

BAUD = 115200
PREFIX = "@CM "
# USB-Seriell-Chips gaengiger ESP32-Boards
KNOWN_USB_IDS = {(0x1A86, 0x7523), (0x1A86, 0x55D4), (0x10C4, 0xEA60), (0x0403, 0x6001), (0x303A, 0x1001)}


def find_port():
    ports = list(serial.tools.list_ports.comports())
    candidates = [p for p in ports if (p.vid, p.pid) in KNOWN_USB_IDS]
    if len(candidates) == 1:
        return candidates[0].device
    if not candidates:
        candidates = ports
    if not candidates:
        sys.exit("Kein COM-Port gefunden. Ist der ESP32 angesteckt?")
    print("Gefundene Ports:")
    for i, p in enumerate(candidates):
        print(f"  [{i}] {p.device}  {p.description}")
    choice = input("Nummer waehlen: ").strip()
    return candidates[int(choice)].device


class Miner:
    def __init__(self, port):
        # DTR/RTS vor dem Oeffnen abschalten, damit der ESP32 nicht neu startet
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = BAUD
        self.ser.timeout = 0.2
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()

    def close(self):
        self.ser.close()

    def command(self, payload, timeout=5.0, verbose=False):
        """Sendet ein JSON-Kommando und wartet auf die @CM-Antwort."""
        self.ser.reset_input_buffer()
        self.ser.write((json.dumps(payload) + "\n").encode())
        end = time.time() + timeout
        while time.time() < end:
            line = self.ser.readline().decode("utf-8", "replace").strip()
            if not line:
                continue
            if line.startswith(PREFIX):
                return json.loads(line[len(PREFIX):])
            if verbose:
                print("  ", line)
        return None

    def wait_ready(self, timeout=40.0):
        """Wartet, bis die Firmware antwortet (z.B. waehrend sie noch bootet)."""
        end = time.time() + timeout
        while time.time() < end:
            resp = self.command({"cmd": "info"}, timeout=2.0)
            if resp and resp.get("ok"):
                return resp
        return None

    def watch_boot(self, timeout=60.0, log=lambda line: print("  ", line)):
        """Liest die Startmeldungen nach einem Neustart und meldet IP bzw. Portal."""
        end = time.time() + timeout
        while time.time() < end:
            line = self.ser.readline().decode("utf-8", "replace").strip()
            if not line:
                continue
            # Meldungen verschiedener Tasks koennen sich mischen -> ueberall in der Zeile suchen
            keys = ("[WiFi]", "[Stratum] Autorisierung", "[Web] Dashboard", "[Kalibrierung]",
                    "Grenze:", "WLAN:", "Passwort:", "Seite:", "PORTAL", "FEHL")
            if any(k in line for k in keys):
                log(line)
            if any(k in line for k in ("Autorisierung OK", "Seite:")):
                return True
        return False


def ask(label, current, secret=False, required=True):
    hint = " [unveraendert]" if secret and current else (f" [{current}]" if current else "")
    while True:
        value = getpass.getpass(f"{label}{hint}: ") if secret else input(f"{label}{hint}: ").strip()
        if value:
            return value
        if current or not required:
            return current if not secret else ""
        print("  Pflichtfeld.")


def main():
    ap = argparse.ArgumentParser(description="ESPressMiner32 ueber die serielle Schnittstelle einrichten")
    ap.add_argument("--port", help="COM-Port, z.B. COM22 (ohne Angabe wird gesucht)")
    ap.add_argument("--get", action="store_true", help="aktuelle Einstellungen anzeigen")
    ap.add_argument("--reset", action="store_true", help="Einstellungen loeschen (Neustart ins Portal)")
    ap.add_argument("--restart", action="store_true", help="Miner neu starten")
    ap.add_argument("--hostname")
    ap.add_argument("--ssid", help="WLAN-Name")
    ap.add_argument("--password", help="WLAN-Passwort")
    ap.add_argument("--pool", help="Primary Pool, z.B. stratum+tcp://pool.example.com:3333")
    ap.add_argument("--wallet", help="Wallet-Adresse[.Worker] fuer den Primary Pool")
    ap.add_argument("--pool-pass", default=None, help="Pool-Passwort (Standard x)")
    ap.add_argument("--pool2", help="Secondary Pool (leer = keiner)")
    ap.add_argument("--wallet2", help="Wallet-Adresse fuer den Secondary Pool")
    ap.add_argument("--pool2-pass", default=None)
    ap.add_argument("-y", "--yes", action="store_true", help="ohne Rueckfrage speichern")
    args = ap.parse_args()

    port = args.port or find_port()
    print(f"Verbinde mit {port} ...")
    miner = Miner(port)
    try:
        info = miner.wait_ready()
        if not info:
            sys.exit("Keine Antwort vom Miner. Laeuft die ESPressMiner32-Firmware? Serial Monitor geschlossen?")
        print(f"{info['name']} v{info['version']}  MAC {info['mac']}  "
              f"{'konfiguriert' if info['configured'] else 'NICHT konfiguriert'}")

        if args.reset:
            resp = miner.command({"cmd": "reset"})
            print(resp.get("msg") if resp else "Keine Antwort")
            miner.watch_boot()
            return
        if args.restart:
            miner.command({"cmd": "restart"})
            print("Neustart ...")
            miner.watch_boot()
            return

        cfg = miner.command({"cmd": "get"})["config"]
        pools = cfg["pools"]

        if args.get:
            print(json.dumps(cfg, indent=2, ensure_ascii=False))
            return

        interactive = not (args.ssid and args.pool and args.wallet)
        if interactive:
            print("\nWerte eingeben (Enter = Vorgabe uebernehmen):")
        new = {
            "cmd": "set",
            "hostname": args.hostname or (ask("Hostname", cfg["hostname"]) if interactive else cfg["hostname"]),
            "wifiSsid": args.ssid or ask("WLAN-Name (SSID)", cfg["wifiSsid"]),
            "wifiPass": args.password if args.password is not None else
                        (ask("WLAN-Passwort", cfg["wifiPassSet"], secret=True) if interactive else ""),
            "pools": [
                {
                    "url": args.pool or ask("Primary Pool URL", pools[0]["url"]),
                    "user": args.wallet or ask("Primary Wallet-Adresse", pools[0]["user"]),
                    "pass": args.pool_pass or (ask("Primary Pool-Passwort", pools[0]["pass"] or "x")
                                               if interactive else pools[0]["pass"] or "x"),
                },
                {
                    "url": args.pool2 if args.pool2 is not None else
                           (ask("Secondary Pool URL (leer = keiner)", pools[1]["url"], required=False)
                            if interactive else pools[1]["url"]),
                    "user": args.wallet2 if args.wallet2 is not None else pools[1]["user"],
                    "pass": args.pool2_pass or pools[1]["pass"] or "x",
                },
            ],
        }
        if interactive and new["pools"][1]["url"]:
            new["pools"][1]["user"] = ask("Secondary Wallet-Adresse", new["pools"][1]["user"] or new["pools"][0]["user"])
            new["pools"][1]["pass"] = ask("Secondary Pool-Passwort", new["pools"][1]["pass"])

        print("\nNeue Einstellungen:")
        print(f"  Hostname:  {new['hostname']}")
        print(f"  WLAN:      {new['wifiSsid']}  (Passwort {'neu' if new['wifiPass'] else 'unveraendert'})")
        for i, name in enumerate(("Primary", "Secondary")):
            p = new["pools"][i]
            print(f"  {name:<9}  {p['url'] or '-'}  {p['user']}")
        if not args.yes and input("Speichern und neu starten? [J/n] ").strip().lower() not in ("", "j", "ja", "y", "yes"):
            print("Abgebrochen.")
            return

        resp = miner.command(new)
        if not resp or not resp.get("ok"):
            sys.exit(f"Fehler: {resp.get('error') if resp else 'keine Antwort'}")
        print(resp["msg"])
        miner.watch_boot()
    finally:
        miner.close()


if __name__ == "__main__":
    main()
