# ESPHome Zeico / Dream Maker Fan Integration

Experimental, cloud-free ESPHome integration for Zeico / Dream Maker smart fans. Replaces stock Tuya firmware to enable local control of power, speed (1-100%), oscillation, and sensors via a wired MCU-UART connection (9600 baud). Note: Developed from raw logs and generated code; currently untested on live hardware. Help wanted!

> **⚠️ Beta / Untested Status**
> This integration has been reverse-engineered and written based on raw UART protocol captures, firmware dumps, and code generation. It **has not yet been tested in the wild** on live hardware. Use with caution, and be prepared to monitor logs during your first flash!

## 🚀 Features (Decoded via UART Protocol)

The component implements a custom state machine to handle the proprietary `FA CE` magic header protocol at `9600 baud`:

* **Full Fan Control:** Power (ON/OFF), Speed percentage (1-100%), and Mode selection (`direct`, `natural`/wind, `smart`/night).
* **Hardware Switches:** Core toggles for Oscillation, Child Lock, Status LED, and Buzzer Sounds.
* **Onboard Sensors:** Real-time feedback for Room Temperature, Relative Humidity, and current Oscillation/Roll Angle.

## 🛠️ Repository Structure

To use this as an `external_component`, ensure your repository directory matches the standard ESPHome layout:

```text
esphome-dreammaker-fan/
├── LICENSE
├── README.md
├── dm_fan.yaml            # Example configuration for your ESP32 device
└── components/
    └── dm_fan/
        ├── __init__.py    # Python configuration and component wiring
        ├── dm_fan.h       # C++ core state machine and UART parsing logic
        └── fan.py         # Empty placeholder required by ESPHome component architecture

```
## ⚡ Hardware Flashing Guide [TBC]

> **⚠️ TO BE CONFIRMED (TBC):** The following flashing instructions are based on community findings and hardware teardowns (see [Issue #50](https://github.com/dhewg/esphome-miot/issues/50)). They have not yet been independently verified for this specific ESPHome implementation. Proceed with caution!

### 1. ⚠️ Stop the ESP32 Reboot Loop (Halt the MCU)
Users have reported that the ESP32 constantly reboots when attempting to read/write via serial. This is caused by the main MCU interfering with the ESP32's boot process. You must halt the MCU during the backup and flashing process:
* Locate the 5-pin header marked **`J3`** right next to the **ROHS** logo on the fan's PCB.
* Find the **Reset** and **GND** pins on this header.
* **Short the Reset pin to GND** using a jumper wire. Keep it shorted the entire time you are backing up or flashing the ESP32!

### 2. ESP32 Hardware Connection
You do not need to solder directly to the ESP32 chip's tiny pins for the initial flash.
* Locate the exposed solder pads on the bottom left of the circuit board.
* They are conveniently labeled as `TX`, `RX`, and `GND`.
* Connect a **3.3V USB-to-TTL Serial Adapter** to these pads:
  * Adapter `RX` ➔ Board `TX`
  * Adapter `TX` ➔ Board `RX`
  * Adapter `GND` ➔ Board `GND`

*(Note: While the ESP32 uses `GPIO16` and `GPIO17` internally to talk to the fan's MCU, the exposed `TX`/`RX` pads are meant for the standard hardware serial bootloader.)*

### 3. Backup the Original Firmware
Before flashing ESPHome, it is highly recommended to create a full dump of the original Tuya/DM-Maker firmware using `esptool.py`. Do not proceed to flash ESPHome unless you have successfully verified the backup bin file!

### 4. Flashing ESPHome
1. Ensure the MCU is still shorted to GND (Step 1).
2. Put the ESP32 into bootloader mode (usually by pulling `GPIO0` to `GND` while booting).
3. Flash your compiled `dm_fan.bin` via the serial adapter.
4. Once the initial serial flash is successful, remove all jumper wires. All future updates can be done comfortably Over-The-Air (OTA) via Home Assistant/ESPHome.


## 📦 How to Integrate

Once this repository is live on GitHub, anyone can easily pull the component directly into their ESPHome configuration without downloading files manually:

```yaml
external_components:
  - source:
      type: git
      url: [https://github.com/d0np3p3/esphome-dreammaker-fan](https://github.com/d0np3p3/esphome-dreammaker-fan)
    components: [ dm_fan ]

uart:
  id: uart_mcu
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

dm_fan:
  id: fan_hub
  uart_id: uart_mcu

# ... reference your switches, sensors, and numbers as shown in dm_fan.yaml

```

## 🤝 Help Wanted & Unconfirmed Details

Since this project is in an experimental stage, early adopters and testers are highly welcome!

Specifically, the following data fields need live validation via the ESPHome `DEBUG` logs:

1. **Temperature & Humidity Bytes:** Currently mapped to bytes 12 and 15 based on plausibility. We need verification if these match actual ambient conditions.
2. **Peripheral Switches:** The exact byte triggers for *Child Lock*, *LED Lights*, and *Buzzer Sounds* are built according to firmware tables but require physical testing on the fan unit to confirm they react correctly.

### 🔴 Note on the Bluetooth Remote Control

Initial attempts to spoof the physical BLE remote control (`MAC 84:0A:10:78:19:33`) revealed that the remote utilizes the **Tuya Beacon Protocol** with secure **XXTEA encryption**. It relies on an ephemeral, encrypted challenge-response handshake during pairing. Because of this security layer, **the stock physical Bluetooth remote is entirely ignored by this component**. Control is intended solely via Home Assistant or alternative smart switches (Zigbee, etc.).

If you test this on your fan, please **open an Issue** or submit a **Pull Request** with your log readouts so we can refine the byte mapping!

## 🎉 Credits & Acknowledgments

This project was a collaborative reverse-engineering effort and builds upon the fantastic groundwork of the open-source community:

* **[d0np3p3](https://github.com/d0np3p3)**: Hardware hacking, raw UART captures, flashing, and project initiation.
* **Claude (Claude AI)**: Heavy lifting on the deep protocol analysis, Tuya BLE Beacon XXTEA encryption discovery, and UART payload mapping.
* **Gemini (Google AI)**: ESPHome 2026.x `external_component` architecture, Python codegen (`__init__.py`), and C++ boilerplate generation.
* **[klada/dmiot2mqtt](https://github.com/klada/dmiot2mqtt)** & **[rapajim/dmiot2mqtt](https://github.com/rapajim/dmiot2mqtt)**: For their foundational work on decoding the proprietary Dream Maker / Zeico MCU protocols.
* **BobeOlsen on [dhewg/esphome-miot (Issue #50)](https://github.com/dhewg/esphome-miot/issues/50)**: For the initial inspiration and hardware discussions that paved the way!
