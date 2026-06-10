# DreamMaker Fan — Open Tasks

## 🔴 Prio 1: BLE Remote Button-Capture

- [ ] `ble_capture.yaml` flashen (aktueller Stand: `2fdb338`)
- [ ] Log prüfen — erwartete Sequenz:
  ```
  FF01 CHALLENGE-1 [20]: ... → state=1, echoing to FF02
  AUTO-ECHO: sent challenge to FF02
  FF01 POST-BIND  [20]: ... → state=2, SMP triggered
  SMP: esp_ble_set_encryption → OK
  [D][BT_SMP] start enc ...
  FF01 EVENT ★ [N bytes]: ...  ← Tastendruck!
  ```
- [ ] Jeden Button einmal drücken + Hex-Payload in `PROTOCOL.md` eintragen
  (Power, Speed+/−, Mode, Oszillation, Winkel, Timer, Sound, LED, Kindersicherung)
- [ ] Falls State=2 aber keine Tasten: **HS10 "Trigger SMP"** in HA drücken
- [ ] Falls Verbindung nach State=2 trennt: SMP deaktivieren (esp_ble_set_encryption-Zeile auskommentieren), dann Tasten testen

---

## 🟡 Prio 2: Remote UART sniffing (Pico W, 2 Drähte)

Lötpunkte: **TX** + **GND** auf dem Remote-Debug-Header (9-Pad-Raster, beschriftet)

- [ ] 0.25 mm Kynar-Draht auf TX und GND löten
- [ ] Remote TX → Pico W GP1 (UART RX)
- [ ] Remote GND → Pico W GND
- [ ] MicroPython-Script auf Pico laden:
  ```python
  from machine import UART, Pin
  uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1), timeout=10)
  while True:
      data = uart.read(64)
      if data:
          print(" ".join("{:02X}".format(b) for b in data),
                "".join(chr(b) if 0x20<=b<0x7F else "." for b in data))
  ```
- [ ] Remote-Batterie rein, Pico per USB → `mpremote connect` oder PuTTY
- [ ] Button drücken und Output beobachten
  - Ausgabe vorhanden → Button-Codes direkt sichtbar ✓
  - Nur Boot-Banner → UART aktiv aber kein Button-Log
  - Nichts → UART im Release-Build deaktiviert → weiter zu Prio 3
- [ ] Falls Müll: Baudrate testen (57600, 38400, 9600)

---

## 🟢 Prio 3: Firmware-Dump via SWD (wenn ST-Link V2 da)

Lötpunkte: **SWC**, **SWD**, **GND** (+ optional **RST**) auf Debug-Header

- [ ] Drähte auf SWC, SWD, GND löten
- [ ] **Option A — ST-Link V2 + SmartSnippets Toolbox** (empfohlen):
  - SmartSnippets Toolbox installieren (kostenlos, Renesas/Dialog)
  - SWC→SWCLK, SWD→SWDIO, GND→GND
  - "Read SPI Flash" → `remote_flash.bin`
- [ ] **Option B — Pico W als picoprobe** (falls ST-Link nicht klappt):
  - picoprobe-UF2 auf Pico flashen
  - SWC→GP3, SWD→GP2, GND→GND
  - OpenOCD: `openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/cortex_m.cfg`
  - RAM/OTP lesen; SPI-Flash via CPU-Register
- [ ] Firmware-Binary analysieren:
  ```bash
  strings remote_flash.bin | grep -iE "button|press|speed|power|mode"
  binwalk remote_flash.bin
  ```

---

## 🔵 Prio 4: Phase 3 — dm_fan.h Integration

(erst wenn Button-Payload-Tabelle vollständig)

- [ ] `PROTOCOL.md` Per-Button-Tabelle ausfüllen
- [ ] Nativen GATTC-Handler in `dm_fan.h` implementieren:
  - `on_connect` → FF01 lesen → Challenge-Echo an FF02
  - `on_gattc_notify` (FF01) → Payload dekodieren → Fan-Aktion aufrufen
  - State-Machine: IDLE → ECHO_SENT → BOUND
- [ ] `ble_remote: true` in `dm_fan.yaml` aktivieren
- [ ] `ble_capture.yaml` entfernen / archivieren
- [ ] Tag `v4.0.0` erstellen (stable release)
