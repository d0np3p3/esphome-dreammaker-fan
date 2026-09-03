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
| `mcu_version` | hardware-bestätigt: liefert `fan_0001` |
| Backport nach `main` | v3.1.0 — kompiliert, bootet, steuert, `mcu_version` läuft |
| Smart-Modus | Speed ist MCU-eigen, wird nicht mehr überschrieben |

### Branch-Aufteilung

- **`main`** — Standardbetrieb ohne Fernbedienung (`dm_fan.yaml`)
- **`v4.0.0-beta`** — alles mit Fernbedienung (`remote_control.yaml`)

Grund: der `ble_key` lässt sich nur **vor** dem Flashen auslesen. Damit ist das
Feature nichts für die stabile Linie, solange es keinen anderen Weg zum
Schlüssel gibt.

---

## 🟡 Offen: Hardware-Gegenprüfung

- [x] **`mcu_version` bestätigt (2026-08-30).** Nach einem Neubau MIT
      `mcu_version:` liefert der Sensor in Home Assistant `fan_0001`. Die MCU
      beantwortet die Boot-Statusabfrage (`0x232A`) also, und
      `on_boot_response_()` extrahiert den Marker korrekt.
      Frühere Logs zeigten den Sensor nur deshalb nicht, weil die Binary
      unverändert war — eine bearbeitete YAML allein reicht nicht, es braucht
      einen neuen Build.

- [ ] Smart-Modus: zeigt HA jetzt die tatsächlich geregelte Drehzahl und folgt
      ihr, wenn Temperatur/Luftfeuchtigkeit sich ändern?
      Debug-Log: `Smart mode: MCU regulated speed to X% (we showed Y%)`
- [x] **`byte[5]` geklärt: der `roll_angle`-Slot.** Die Beacon-Payload spiegelt
      Feld für Feld das UART-State-Layout (`power, speed, mode, roll_enable,
      roll_angle, power_delay`) — fünf von sechs Feldern stimmen überein, die
      Lücke fällt genau auf `roll_angle`. Positionsbeweis, keine Vermutung.
      Bleibt `0x00`, weil diese Fernbedienung den Winkel weder steuern noch
      anzeigen kann (4 Tasten, Head-shaking nur Ein/Aus ohne Langdruck, keine
      Winkel-LED) — sie kennt den Wert also gar nicht.
      **Nichts zu implementieren:** `0x00` als Winkel zu senden wäre falsch.
      Der Winkel bleibt HA-seitig steuerbar, das funktioniert bereits.
      Die Diagnose-Warnung bleibt drin, falls ein anderer Sender das Feld füllt.

---

## 🔵 Wieder offen: der Schlüssel — nRF52840-Sniffer unterwegs

Mit einem Sniffer wird die Frage erneut entscheidbar. Bisher haben wir immer
nur **unsere eigene** GATT-Sitzung mit der Fernbedienung gesehen — das
eigentliche Bind-Gespräch zwischen Remote und Original-Fan ist komplett
unbeobachtet, und genau dort muss der Schlüssel entstehen.

Günstig: die DA14580 kann nur **Legacy Pairing** (LE Secure Connections kam erst
mit BLE 4.2), und das ist mit Wireshark entschlüsselbar, sofern der Mitschnitt
vor dem Verbindungsaufbau startet.

- [ ] Testprotokoll durchführen:
      [`docs/nrf-sniffer-bind-capture.md`](docs/nrf-sniffer-bind-capture.md)

### Erster Mitschnitt ausgewertet (2026-09-03) — noch keine Tastendrücke drin

`remote.pcap`, 27,5 s, 6830 Frames. Aufgenommenes Gerät ist **Remote #2**
(`84:0A:10:78:19:33`), nicht ein Ventilator — die Company ID `0x4D44` gehört
DreamMaker, und der Fan sendet dieses Advertisement nicht.

**Kein einziger Frame mit `status=0x02`.** Der variable Wert bei Offset 8
(`0x28→0x29`) ist der Sequenzzähler, nicht der Status; Offset 9 blieb über alle
6830 Frames auf `0x01`. Bytes 10–17 sind deshalb null — genau so steht es in der
PROTOCOL.md-Tabelle. Fehlender GATT-Verkehr ist ebenfalls erwartbar: Tasten
laufen über Advertising, nie über GATT.

Das Verhalten passt exakt auf eine **ungebundene** Remote (Befund 2026-06-01:
ungebunden → Heartbeats plus Nullpayload, auch beim Tastendruck). Der
Zählerschritt bei t=12,13 s ist also ein Tastendruck ohne Peer.

- [ ] ⚠️ **Klären, ob in dieser Sitzung *Power + M* gedrückt wurde.** Reset und
      Pairing sind dieselbe Aktion — die Kombination löst den Bind auch dann,
      wenn kein Fan das Pairing abschließt. Falls ja, beschreibt der `ble_key`
      in Fan #3 keinen künftigen Verkehr von Remote #2 mehr.
- [ ] **NVS von Fan #3 sichern — unabhängig davon, zuerst.** Bleibt in jedem
      Fall die Ground Truth für das 19-Payload-Corpus vom 2026-08-10 und ist das
      einzige bekannte (Capture, Key)-Paar des Projekts.
- [ ] Mitschnitt A (Tasten-Corpus mit Zeitprotokoll) und B (Bind gegen Fan #4),
      in dieser Reihenfolge — B drückt *Power + M* und beendet den Bind, auf den
      A angewiesen ist:
      [`docs/nrf-sniffer-remote-capture.md`](docs/nrf-sniffer-remote-capture.md)

Fürs nächste Mal: **ESPHome-Knoten vorher abschalten.** Eine gehaltene
GATT-Verbindung bringt die Remote zum Schweigen, und im Trace scannt ein
Espressif-Gerät (`98:F4:AB:3C:9D:D6`) mit — vermutlich unser eigenes.

### Was bereits ausgeschlossen ist (nicht nochmal testen)

**Ergebnis vom 2026-08-10: die FF01-Hypothese ist widerlegt.**

Remote `84:0A:10:78:19:33` wurde an einen Original-FW-Fan gekoppelt und ihre
Kommando-Beacons mitgeschnitten (19 Payloads, 18 verschiedene). Dagegen getestet
wurde alles, was sich aus den GATT-Daten ableiten lässt:

- alle 13 Acht-Byte-Fenster der 20-Byte-FF01-Nachricht
- FF01 rückwärts, gekürzt, nullgepolstert
- die Remote-MAC, rückwärts und beidseitig nullgepolstert
- MD5/SHA1/SHA256 über FF01, FF01[0:8] und die MAC
- der bekannte `ble_key` der anderen Remote als Kontrolle

**Bester Treffer: 1 von 19 Prüfsummen — exakt Zufallsniveau.** Zum Vergleich:
der richtige Schlüssel erreichte beim früheren Capture 18/18.

**Der Schlüssel entsteht beim Pairing und liegt ausschließlich im NVS des Fans.**
Nicht ableitbar aus der Remote, nicht aus ihrer Adresse, nicht aus der Cloud
(alles einzeln geprüft). Auslesen geht nur an einem Fan mit Original-Firmware.

### Was das für das Feature bedeutet

Die NVS-Voraussetzung ist real und mit dem heutigen Wissensstand unumgehbar.
Damit bleibt die Aufteilung wie sie ist: `main` ohne Fernbedienung,
`v4.0.0-beta` mit — und die Doku muss deutlich sagen, dass **vor** dem Flashen
gesichert werden muss.

### Nebenbefunde aus der Testreihe

- ESPHome kann sich als GATT-Client mit der Remote verbinden (Service Discovery,
  Notify-Registrierung, Writes — alles akzeptiert)
- **FF01 notifiziert nie.** Die Registrierung gelingt, gepusht wird nichts —
  auch nicht bei Power+M. Alle je gesehenen Werte kamen vom Polling.
- **Ein ESPHome-seitiger Bind ist nicht möglich.** Echo auf FF02 (16× getestet)
  bindet nicht; im Handbuch wird das Pairing durch einen Tastendruck **am Fan**
  abgeschlossen, wofür es hier kein Gegenstück gibt.
- **Verbunden = kein Advertising.** Solange der ESP eine GATT-Verbindung hält,
  sendet die Remote keine Beacons. Lesen und Mitschneiden schließen sich aus.
- Struktur `8|6|6` der FF01-Nachricht auf zwei unabhängigen Geräten bestätigt

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
