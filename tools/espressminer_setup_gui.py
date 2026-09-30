#!/usr/bin/env python3
"""
ESPressMiner32 Setup (GUI) - Zugangsdaten per USB/COM-Port auf den Miner schreiben.

Start:  python espressminer_setup_gui.py        (benoetigt pyserial: pip install pyserial)

Die JSON-Dateien haben dasselbe Format wie /api/config der Webseite, z.B.:
{
  "hostname": "espressminer",
  "wifiSsid": "MeinWLAN",
  "wifiPass": "geheim",
  "pools": [
    {"url": "stratum+tcp://pool.example.com:3333", "user": "ADRESSE.Worker", "pass": "x"},
    {"url": "", "user": "", "pass": "x"}
  ]
}
"""

import json
import queue
import threading
import tkinter as tk
import webbrowser
from tkinter import filedialog, messagebox, ttk

import serial.tools.list_ports

from espressminer_setup import KNOWN_USB_IDS, Miner

# Passt zum blauen Web-Dashboard
REPO_URL = "https://github.com/brenner23/ESPressMiner32"
BG, CARD, FIELD, TXT, DIM = "#161821", "#222530", "#191c26", "#e8eaf0", "#9aa0b0"
ACCENT, ACCENT2, IBD = "#3d8bfd", "#2f6fd6", "#3a3f50"
GREEN, RED, ORANGE = "#3fb950", "#e5534b", "#3d8bfd"


class SetupApp:
    def __init__(self, root):
        self.root = root
        self.events = queue.Queue()      # (Funktion, Argumente) aus dem Worker-Thread
        self.busy = False
        self.wifi_pass_set = False

        root.title("ESPressMiner32 Setup")
        root.configure(bg=BG)
        root.minsize(620, 640)
        self._style()
        self._build()
        self.refresh_ports()
        root.after(100, self._poll_events)

    # ------------------------------------------------------------------ UI
    def _style(self):
        s = ttk.Style()
        s.theme_use("clam")
        s.configure(".", background=BG, foreground=TXT, fieldbackground=FIELD, font=("Segoe UI", 10))
        s.configure("Card.TFrame", background=CARD)
        s.configure("Card.TLabel", background=CARD, foreground=ACCENT, font=("Segoe UI", 10, "bold"))
        s.configure("Head.TLabel", background=BG, foreground=ACCENT, font=("Segoe UI", 11, "bold"))
        s.configure("Title.TLabel", background=BG, foreground=TXT, font=("Segoe UI", 16))
        s.configure("Status.TLabel", background=BG, foreground=DIM)
        s.configure("TEntry", foreground=TXT, insertcolor=TXT, bordercolor="#555", lightcolor="#555")
        s.map("TEntry", bordercolor=[("focus", ACCENT)])
        s.configure("TCombobox", foreground=TXT, arrowcolor=TXT, background="#3a3a3a")
        s.map("TCombobox", fieldbackground=[("readonly", FIELD)], foreground=[("readonly", TXT)],
              selectbackground=[("readonly", FIELD)], selectforeground=[("readonly", TXT)])
        self.root.option_add("*TCombobox*Listbox.background", FIELD)
        self.root.option_add("*TCombobox*Listbox.foreground", TXT)
        self.root.option_add("*TCombobox*Listbox.selectBackground", ACCENT)
        s.configure("TButton", background="#3a3a3a", foreground=TXT, borderwidth=0, padding=(12, 7))
        s.map("TButton", background=[("active", "#4a4a4a"), ("disabled", "#2a2a2a")],
              foreground=[("disabled", DIM)])
        s.configure("Save.TButton", background=ACCENT, foreground="white", font=("Segoe UI", 10, "bold"))
        s.map("Save.TButton", background=[("active", ACCENT2), ("disabled", "#24406f")])
        s.configure("TCheckbutton", background=CARD, foreground=DIM)
        s.map("TCheckbutton", background=[("active", CARD)])

    def _build(self):
        pad = {"padx": 16}
        title = ttk.Frame(self.root)
        title.pack(fill="x", pady=(14, 6), **pad)
        tk.Label(title, text="ESPress", fg=ORANGE, bg=BG, font=("Segoe UI", 18, "bold")).pack(side="left")
        tk.Label(title, text="Miner32 Setup", fg=TXT, bg=BG, font=("Segoe UI", 18)).pack(side="left")

        # COM-Port
        portrow = ttk.Frame(self.root)
        portrow.pack(fill="x", pady=(4, 8), **pad)
        ttk.Label(portrow, text="COM-Port:").pack(side="left")
        self.port = ttk.Combobox(portrow, width=34, state="readonly")
        self.port.pack(side="left", padx=8)
        ttk.Button(portrow, text="↻", width=3, command=self.refresh_ports).pack(side="left")
        self.btn_read = ttk.Button(portrow, text="Vom Miner lesen", command=self.read_from_miner)
        self.btn_read.pack(side="right")

        # Felder
        self.vars = {}
        form = ttk.Frame(self.root)
        form.pack(fill="x", **pad)
        self._field(form, "hostname", "Hostname:")
        self._field(form, "wifiSsid", "WiFi SSID:")
        self._field(form, "wifiPass", "WiFi Password:", secret=True)
        ttk.Label(form, text="▼  Primary Pool Settings", style="Head.TLabel").pack(anchor="w", pady=(10, 0))
        self._field(form, "p0url", "Pool URL:")
        self._field(form, "p0user", "Wallet Address:")
        self._field(form, "p0pass", "Pool Password:")
        ttk.Label(form, text="▼  Secondary Pool Settings", style="Head.TLabel").pack(anchor="w", pady=(10, 0))
        self._field(form, "p1url", "Pool URL:")
        self._field(form, "p1user", "Wallet Address:")
        self._field(form, "p1pass", "Pool Password:")
        self.vars["hostname"].set("espressminer")
        self.vars["p0pass"].set("x")
        self.vars["p1pass"].set("x")

        # Buttons
        btns = ttk.Frame(self.root)
        btns.pack(fill="x", pady=10, **pad)
        ttk.Button(btns, text="JSON laden ...", command=self.load_json).pack(side="left")
        ttk.Button(btns, text="JSON speichern ...", command=self.save_json).pack(side="left", padx=6)
        self.btn_reset = ttk.Button(btns, text="Zurücksetzen (Portal)", command=self.reset_miner)
        self.btn_reset.pack(side="left")
        self.btn_save = ttk.Button(btns, text="Speichern & Neustart", style="Save.TButton",
                                   command=self.write_to_miner)
        self.btn_save.pack(side="right")

        # Protokoll
        self.log = tk.Text(self.root, height=9, bg=FIELD, fg=TXT, insertbackground=TXT, relief="flat",
                           font=("Consolas", 9), wrap="word", state="disabled")
        self.log.pack(fill="both", expand=True, padx=16, pady=(0, 6))
        self.log.tag_configure("err", foreground=RED)
        self.log.tag_configure("ok", foreground=GREEN)
        self.status = ttk.Label(self.root, text="Bereit.", style="Status.TLabel")
        self.status.pack(fill="x", padx=16, pady=(0, 4))
        link = tk.Label(self.root, text=REPO_URL, fg=DIM, bg=BG, cursor="hand2", font=("Segoe UI", 9, "underline"))
        link.pack(anchor="w", padx=16, pady=(0, 10))
        link.bind("<Button-1>", lambda _e: webbrowser.open(REPO_URL))

    def _field(self, parent, key, label, secret=False):
        row = tk.Frame(parent, bg=CARD, highlightthickness=0)
        row.pack(fill="x", pady=4)
        tk.Frame(row, bg=ACCENT, width=4).pack(side="left", fill="y")
        ttk.Label(row, text=label, style="Card.TLabel", width=16).pack(side="left", padx=(12, 6), pady=9)
        var = tk.StringVar()
        entry = ttk.Entry(row, textvariable=var, show="•" if secret else "")
        entry.pack(side="left", fill="x", expand=True, padx=(0, 10), pady=7)
        if secret:
            show = tk.BooleanVar(value=False)
            ttk.Checkbutton(row, text="anzeigen", variable=show, style="TCheckbutton",
                            command=lambda: entry.configure(show="" if show.get() else "•")
                            ).pack(side="left", padx=(0, 10))
            self.pass_hint = tk.Label(parent, text="", fg=DIM, bg=BG, font=("Segoe UI", 8))
            self.pass_hint.pack(anchor="e")
        self.vars[key] = var

    # ------------------------------------------------------------------ Hilfen
    def refresh_ports(self):
        ports = list(serial.tools.list_ports.comports())
        ports.sort(key=lambda p: (p.vid, p.pid) not in KNOWN_USB_IDS)   # ESP32-Adapter zuerst
        self.port_devices = [p.device for p in ports]
        self.port["values"] = [f"{p.device}  -  {p.description}" for p in ports]
        if ports:
            self.port.current(0)
        self.set_status(f"{len(ports)} COM-Port(s) gefunden." if ports else "Kein COM-Port gefunden.")

    def selected_port(self):
        i = self.port.current()
        return self.port_devices[i] if i >= 0 else None

    def set_status(self, text):
        self.status.configure(text=text)

    def write_log(self, text, tag=None):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n", tag)
        self.log.see("end")
        self.log.configure(state="disabled")

    def set_busy(self, busy):
        self.busy = busy
        state = "disabled" if busy else "normal"
        for b in (self.btn_read, self.btn_save, self.btn_reset):
            b.configure(state=state)

    def _poll_events(self):
        while not self.events.empty():
            fn, args = self.events.get()
            fn(*args)
        self.root.after(100, self._poll_events)

    def ui(self, fn, *args):
        """Aus dem Worker-Thread eine Aktion im UI-Thread ausfuehren."""
        self.events.put((fn, args))

    def run_worker(self, job, busy_text):
        port = self.selected_port()
        if not port:
            messagebox.showerror("ESPressMiner32", "Bitte einen COM-Port wählen.")
            return
        self.set_busy(True)
        self.set_status(busy_text)

        def worker():
            miner = None
            try:
                miner = Miner(port)
                self.ui(self.write_log, f"Verbinde mit {port} ...")
                info = miner.wait_ready(timeout=40)
                if not info:
                    raise RuntimeError("Keine Antwort. Läuft die ESPressMiner32-Firmware? "
                                       "Serial Monitor geschlossen?")
                self.ui(self.write_log, f"{info['name']} v{info['version']}  MAC {info['mac']}  "
                        f"({'konfiguriert' if info['configured'] else 'nicht konfiguriert'})")
                job(miner)
            except Exception as e:                      # noqa: BLE001 - Fehler im UI anzeigen
                self.ui(self.write_log, f"Fehler: {e}", "err")
                self.ui(self.set_status, "Fehler.")
            finally:
                if miner:
                    miner.close()
                self.ui(self.set_busy, False)

        threading.Thread(target=worker, daemon=True).start()

    # ------------------------------------------------------------------ Daten
    def to_payload(self):
        v = {k: var.get().strip() for k, var in self.vars.items()}
        return {
            "hostname": v["hostname"],
            "wifiSsid": v["wifiSsid"],
            "wifiPass": self.vars["wifiPass"].get(),
            "pools": [
                {"url": v["p0url"], "user": v["p0user"], "pass": v["p0pass"] or "x"},
                {"url": v["p1url"], "user": v["p1user"], "pass": v["p1pass"] or "x"},
            ],
        }

    def from_payload(self, cfg, keep_password=False):
        self.vars["hostname"].set(cfg.get("hostname", ""))
        self.vars["wifiSsid"].set(cfg.get("wifiSsid", ""))
        if not keep_password:
            self.vars["wifiPass"].set(cfg.get("wifiPass", ""))
        pools = (cfg.get("pools") or []) + [{}, {}]
        for i in range(2):
            self.vars[f"p{i}url"].set(pools[i].get("url", ""))
            self.vars[f"p{i}user"].set(pools[i].get("user", ""))
            self.vars[f"p{i}pass"].set(pools[i].get("pass", "x") or "x")

    def validate(self, p):
        if not p["hostname"]:
            return "Hostname fehlt."
        if not p["wifiSsid"]:
            return "WiFi SSID fehlt."
        if not p["wifiPass"] and not self.wifi_pass_set:
            return "WiFi Password fehlt."
        for i, name in enumerate(("Primary", "Secondary")):
            url, user = p["pools"][i]["url"], p["pools"][i]["user"]
            if i == 0 and not url:
                return "Primary Pool URL fehlt."
            if url and not (url.startswith("stratum+tcp://") and url.rfind(":") > 14):
                return f"{name} Pool URL muss so aussehen: stratum+tcp://host:port"
            if url and not user:
                return f"Wallet Address ({name}) fehlt."
        return None

    # ------------------------------------------------------------------ Aktionen
    def read_from_miner(self):
        def job(miner):
            resp = miner.command({"cmd": "get"})
            if not resp or not resp.get("ok"):
                raise RuntimeError("Einstellungen konnten nicht gelesen werden.")
            cfg = resp["config"]

            def apply():
                self.from_payload(cfg, keep_password=True)
                self.vars["wifiPass"].set("")
                self.wifi_pass_set = cfg.get("wifiPassSet", False)
                self.pass_hint.configure(text="Passwort ist gespeichert - leer lassen = unverändert"
                                         if self.wifi_pass_set else "")
                self.write_log("Einstellungen gelesen.", "ok")
                self.set_status("Einstellungen vom Miner geladen.")
            self.ui(apply)

        self.run_worker(job, "Lese Einstellungen ...")

    def write_to_miner(self):
        payload = self.to_payload()
        error = self.validate(payload)
        if error:
            messagebox.showerror("ESPressMiner32", error)
            return

        def job(miner):
            resp = miner.command({"cmd": "set", **payload})
            if not resp or not resp.get("ok"):
                raise RuntimeError(resp.get("error") if resp else "Keine Antwort.")
            self.ui(self.write_log, "Gespeichert - Miner startet neu ...", "ok")
            self.ui(self.set_status, "Warte auf Neustart ...")
            found = miner.watch_boot(timeout=60, log=lambda line: self.ui(self.write_log, "  " + line))
            self.ui(self.set_status, "Fertig." if found else "Neustart ohne Meldung - bitte prüfen.")

        self.run_worker(job, "Schreibe Einstellungen ...")

    def reset_miner(self):
        if not messagebox.askyesno("ESPressMiner32", "Alle Einstellungen auf dem Miner löschen?\n"
                                   "Er startet danach ins Einrichtungs-Portal."):
            return

        def job(miner):
            resp = miner.command({"cmd": "reset"})
            self.ui(self.write_log, resp.get("msg", "") if resp else "Keine Antwort.", "ok")
            miner.watch_boot(timeout=60, log=lambda line: self.ui(self.write_log, "  " + line))
            self.ui(self.set_status, "Miner zurückgesetzt.")

        self.run_worker(job, "Setze zurück ...")

    def load_json(self):
        path = filedialog.askopenfilename(title="Zugangsdaten laden",
                                          filetypes=[("JSON", "*.json"), ("Alle Dateien", "*.*")])
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as f:
                cfg = json.load(f)
        except (OSError, ValueError) as e:
            messagebox.showerror("ESPressMiner32", f"Datei konnte nicht gelesen werden:\n{e}")
            return
        self.from_payload(cfg)
        self.write_log(f"Geladen: {path}", "ok")

    def save_json(self):
        path = filedialog.asksaveasfilename(title="Zugangsdaten speichern", defaultextension=".json",
                                            initialfile="espressminer_credentials.json",
                                            filetypes=[("JSON", "*.json")])
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.to_payload(), f, indent=2, ensure_ascii=False)
        self.write_log(f"Gespeichert: {path}", "ok")


def main():
    root = tk.Tk()
    SetupApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
