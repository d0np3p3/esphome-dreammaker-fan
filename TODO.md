# DreamMaker Fan — Open Tasks

> **Durchbruch 2026-08-03:** Die 8-Byte-Beacon-Payload ist **DES-ECB** mit dem
> `ble_key` aus dem NVS. Vollständig dekodiert inkl. Prüfsumme — 18/18 Payloads
> konsistent. Lerntabelle wird nicht mehr gebraucht. Siehe PROTOCOL.md.

---

## ✅ Stand 2026-08-03 — hier vorerst fertig

Die Fernbedienung steuert den Fan. Was noch offen ist, steht unten und ist
alles optional oder braucht Hardware-Gegenprüfung.

### Erreicht

| | |
|---|---|
| Beacon-Payload entschlüsselt | **DES-ECB** mit `ble_key` aus dem NVS |
| Payload-Struktur | Taste + kompletter Zielzustand + Prüfsumme, 18/18 konsistent |
| Auf Hardware bestätigt | alle 5 Tasten, keine `checksum mismatch` |
| DES-Implementierung | selbstgeschrieben, gegen FIPS-Vektor + Captures geprüft |
| `0x1F44`-ACK-Bug | gefixt (`data_len=1, data=[01]`) |
| `0x1F41`-Frameformat | hardware-bestätigt (MCU ACKt) — aber wirkungslos |
| Smart-Modus | Speed ist MCU-eigen, wird nicht mehr überschrieben |

### Branch-Aufteilung

- **`main`** — Standardbetrieb ohne Fernbedienung (`dm_fan.yaml`)
- **`v4.0.0-beta`** — alles mit Fernbedienung (`remote_control.yaml`)

Grund: der `ble_key` lässt sich nur **vor** dem Flashen auslesen. Damit ist das
Feature nichts für die stabile Linie, solange es keinen anderen Weg zum
Schlüssel gibt.

---

## 🟡 Offen: Hardware-Gegenprüfung

- [ ] Smart-Modus: zeigt HA jetzt die tatsächlich geregelte Drehzahl und folgt
      ihr, wenn Temperatur/Luftfeuchtigkeit sich ändern?
      Debug-Log: `Smart mode: MCU regulated speed to X% (we showed Y%)`
- [ ] **`byte[5]` klären — die Fernbedienung HAT eine Winkel-Taste.**
      Meine frühere Notiz „keine Winkel-Taste" stammte aus dem DM-FCB01-
      Handbuch, das nur vier Tasten auflistet, und war falsch.
      `byte[5]` war in allen 18 Payloads `0x00` — aber der Winkel wurde nie
      über die Fernbedienung verstellt. Damit ist es der Hauptverdächtige.
      Test: Winkel-Taste drücken, Log auf DEBUG, diese Zeile ansehen:
      `[D] decrypted: XX XX XX XX XX XX XX XX`
      Bei `byte[5]` in {1E,3C,5A,78,8C} ist es der Winkel (30/60/90/120/140°).
      Es warnt zusätzlich automatisch, sobald `byte[5]` erstmals ≠ 0 ist.
- [ ] Winkel dann in `handle_remote_command_` anwenden (`set_roll_angle`) —
      aktuell wird `byte[5]` ignoriert, Winkel-Drücke bleiben also wirkungslos

---

## 🔵 Offen: der Schlüssel für andere Nutzer

Das ist die einzige echte Hürde für eine breitere Nutzung.

**Vielversprechende Spur (unbestätigt):** Die ersten **8 Byte** der
GATT-Bind-Nachricht (FF01) sind genau schlüsselgroß, und die Neu-Gruppierung
`8|6|6` zeigt sie als einziges variables Feld (siehe PROTOCOL.md). Wäre das der
Schlüssel, könnte ESPHome ihn beim eigenen Bind lernen — kein NVS-Dump mehr nötig.

- [ ] **Entscheidender Test:** Fan mit Original-FW koppeln, dabei FF01
      mitschneiden, danach NVS dumpen und `ble_key` gegen die ersten 8 Byte
      vergleichen
      > Die dafür nötige GATT-Capture-Config wurde mit den übrigen
      > Forschungs-YAMLs entfernt — bei Bedarf aus der Git-Historie holen:
      > `git show 29538a7:ble_capture.yaml > ble_capture.yaml`
- [ ] Falls Treffer: Bind in dm_fan.h implementieren → Feature wäre reif für `main`

**Ausgeschlossen:** nicht aus der Cloud (Pairing-Mitschnitt zeigt keinen
Austausch), nicht ableitbar aus MAC/product_id/device_id/device_key
(mehrere hundert Ableitungen getestet).

---

## 📝 Offen: Doku / Community

- [ ] **Korrektur in `dhewg/esphome-miot#50` posten** — Text liegt fertig in
      [`docs/esphome-miot-issue50-correction.md`](docs/esphome-miot-issue50-correction.md)
- [ ] Flash-Backup Hälfte 2 (`0x200000`–`0x400000`) nachziehen
- [ ] Wenn Smart-Modus gegengeprüft: Tag `v4.0.0`

---

## ⚪ Eingestellt

- ~~Lerntabelle~~ — durch DES-Entschlüsselung überflüssig
- ~~Rohes Beacon-Forwarding an die MCU (`0x1F41`)~~ — Format bestätigt, aber
  wirkungslos: das ESP-Modul war die entschlüsselnde Seite, nicht die MCU
- ~~Remote SWD~~ — `chipid: 0x000`, Debug-Port vermutlich gesperrt
- ~~Remote UART~~ — nur Rauschen, kein validiertes Frame
- ~~GATT als Kommandoweg~~ — Challenge-Echo gehört zum Bind, nicht zu den Tasten

---

## 🔒 Sicherheitshinweis

`ble_key`, `device_key`, `device_id` und WiFi-Zugangsdaten sind **pro Gerät
geheim** und stehen **nicht** in diesem öffentlichen Repo — nur Struktur und
Fundort. Der `ble_key` gehört in `secrets.yaml`.
