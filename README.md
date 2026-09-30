# ☕ ESPressMiner32

🇬🇧 **English** · 🇩🇪 [Deutsch](README.de.md)

A Bitcoin Stratum (V1) miner for the **classic ESP32**. It drives the **SHA-256 hardware accelerator**
through direct register access with a pipeline: the data for the next block is written while the engine
is still computing.

Measured on ESP32-D0WD-V3 @ 240 MHz: **~902 kH/s** (265 cycles per double SHA-256).

🔗 **Project page:** https://github.com/brenner23/ESPressMiner32

> ⚠️ **Hobby project.** Realistically an ESP32 will never find a Bitcoin block. This is about learning,
> tinkering and pushing the hardware to its limit. Great for solo pools, lottery mining or your own test pool.
>
> The web UI, setup tools and serial output are in German.

---

## Features

- **~902 kH/s** on a classic ESP32 (hardware SHA, calibrated pipeline)
- **Self-test** at boot (genesis block, 2000 random headers, 5000 nonces). If it fails, the miner does not mine.
- **Every hit is re-checked in software** before a share is sent to the pool
- **Web dashboard** at `http://espressminer.local/` with hashrate history, JSON at `/api/stats`
- **Primary and secondary pool** with automatic failover
- **Setup without coding:** Wi-Fi portal, GUI setup tool or command line
- **Blue LED (GPIO2)** is on while mining and blinks on every share

## Project layout

```
ESPressMiner32/
├── platformio.ini            Build configuration (240 MHz, -O2)
├── include/
│   ├── credentials.h         Default values (empty, optional)
│   ├── config.h              Difficulty, cores, LED, nonce ranges
│   ├── sha256_hw.h           Hardware SHA pipeline
│   ├── sha256_sw.h           Software SHA-256 (coinbase, merkle, verification)
│   ├── stratum_client.h      Stratum V1 client
│   ├── mining_job.h          Job/share exchange between tasks
│   └── ...
├── src/                      Implementations + main.cpp
├── tools/                    USB setup: espressminer_setup_gui.py (GUI), espressminer_setup.py
└── test/hw_probe/            Measurement firmware for the SHA accelerator (development)
```

## Build & flash (VS Code + PlatformIO)

1. Open the folder in VS Code (the PlatformIO extension will be suggested).
2. Adjust `upload_port` / `monitor_port` in `platformio.ini` to your COM port (or delete those lines and let PlatformIO detect it).
3. Click **→ Upload** in the status bar (or `pio run -t upload`).
4. The **serial monitor** (115200 baud) shows the self-test, the benchmark and the hashrate every 5 s.

No credentials in the code: `include/credentials.h` is intentionally empty, everything is configured after flashing.

## First-time setup (after flashing)

As long as no settings are stored, the miner does **not** start mining but waits to be configured.

**1. Wi-Fi portal (phone/laptop)**
Connect to the Wi-Fi `ESPressMiner32-XXXX` (password `espressminer`). The setup page opens automatically,
otherwise go to `http://192.168.4.1/`. Fill in the form, then "Speichern & Neustart" (save & restart).
The portal also starts if the stored Wi-Fi cannot be reached for 20 s.

**2. GUI setup tool** (`pip install pyserial`)
```
python tools/espressminer_setup_gui.py
```
Pick the COM port, fill in the fields (or load a JSON file), save & restart.
Template for multiple miners: `tools/credentials.example.json`.

**3. Command line over USB**
```
python tools/espressminer_setup.py                 # interactive, COM port is detected
python tools/espressminer_setup.py --port COM3 --get
python tools/espressminer_setup.py --ssid MyWiFi --password secret ^
       --pool stratum+tcp://pool.example.com:3333 --wallet ADDRESS.worker -y
python tools/espressminer_setup.py --reset         # erase settings -> portal
```

**4. Raw serial console** (115200 baud), one JSON line per command, replies start with `@CM `:
```
{"cmd":"info"}   {"cmd":"get"}   {"cmd":"reset"}   {"cmd":"restart"}
{"cmd":"set","hostname":"espressminer","wifiSsid":"...","wifiPass":"...",
 "pools":[{"url":"stratum+tcp://host:3333","user":"ADDRESS.worker","pass":"x"},{"url":""}]}
```

**Reset without a PC:** hold the BOOT button for 5 s → settings erased, restart into the portal.

## How it works

| Core | Task |
|------|------|
| 1 | Hardware SHA miner (exclusive) |
| 0 | Wi-Fi, Stratum client, web dashboard, statistics |

- **Settings** (gear icon in the dashboard): hostname, Wi-Fi, primary and secondary pool, stored in flash (NVS).
  After 3 failed attempts the miner switches to the other pool; from the secondary it retries the primary every 10 minutes.
- The dashboard runs on core 0 and costs no hashrate.

## Where the hashrate comes from

| Step | kH/s |
|---|---|
| Plain hardware path (polling BUSY) | ~360 |
| Pipeline: write the next block while the engine computes | ~557 |
| Buffered text register writes without `memw` (idea from SparkMiner) | ~650 |
| Base address kept in a register instead of `l32r` per access | ~700 |
| Fixed wait times instead of BUSY (the flag lags ~34 cycles) | ~800 |
| No `memw` after LOAD | ~833 |
| Wait time calibrated per phase | ~842 |
| Software pipeline: start the next block 1 before the candidate check | **~902** |

The wait times are **calibrated at every boot**: the ESP32 searches for the smallest error-free wait time and
adds `HW_CAL_MARGIN_*` cycles of headroom. If the hardware miscalculates during operation (every hit is
re-checked in software), the wait time is increased automatically.

The classic ESP32 cannot load a SHA midstate back into the engine, so every nonce always needs three hardware blocks.

Measurement tool: `pio run -e hw_probe -t upload` (register access costs, engine timings, phase profile).

## License

[MIT](LICENSE) © 2026 brenner23. Use at your own risk.
Credits and libraries: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
