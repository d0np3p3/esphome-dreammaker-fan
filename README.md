# ESPHome Zeico / Dream Maker Fan Integration

Experimental, cloud-free ESPHome integration for Zeico / Dream Maker smart fans. Replaces stock Tuya firmware to enable local control of power, speed (1-100%), oscillation, and sensors via a wired MCU-UART connection (9600 baud). Note: Developed from raw logs and generated code; currently untested on live hardware. Help wanted!

> **⚠️ Beta / Untested Status** > This integration has been reverse-engineered and written based on raw UART protocol captures, firmware dumps, and code generation. It **has not yet been tested in the wild** on live hardware. Use with caution, and be prepared to monitor logs during your first flash!

## 🚀 Features (Decoded via UART Protocol)

The component implements a custom state machine to handle the proprietary `FA CE` magic header protocol at `9600 baud`:

* **Full Fan Control:** Power (ON/OFF), Speed percentage (1-100%), and Mode selection (`direct`, `natural`/wind, `smart`/night).
* **Hardware Switches:** Core toggles for Oscillation, Child Lock, Status LED, and Buzzer Sounds.
* **Onboard Sensors:** Real-time feedback for Room Temperature, Relative Humidity, and current Oscillation/Roll Angle.

## 🛠️ Repository Structure

To use this as an `external_component`, ensure your repository directory matches the standard ESPHome layout:

```text
esphome-zeico-fan/
├── LICENSE
├── README.md
├── dm_fan.yaml            # Example configuration for your ESP32 device
└── components/
    └── dm_fan/
        ├── __init__.py    # Python configuration and component wiring
        ├── dm_fan.h       # C++ core state machine and UART parsing logic
        └── fan.py         # Empty placeholder required by ESPHome component architecture
