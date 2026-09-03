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

### Randbedingungen 2026-09-03: kein NVS-Zugriff

Der ESP ist zum Auslesen nicht erreichbar, Fan und Remote bleiben vorerst
original, BT-Proxy ist aus.

- [ ] ⭐ **Zuerst, ohne jede Hardware: `dm_ble_key` gegen die Post-Bind-FF01 von
      Remote #1 halten.** Remote #1 ist das laufende Setup, ihr Schlüssel ist
      also bekannt, und ihr Wert *nach* dem Bind steht seit dem 2026-06-10 im
      Repo. Verglichen wurde beides nie.

      ```
      0D 0A 40 15 DC 7C 45 43     (PROTOCOL.md)
      0D 0A 40 15 5D C7 C4 54     (nrf-sniffer-bind-capture.md — die beiden
                                   Dokumente widersprechen sich ab Byte 4)
      ```

      Trifft einer zu, wird der Schlüssel beim Bind an die Remote übergeben und
      ist danach schlicht auslesbar — die NVS-Voraussetzung fällt komplett weg.

**Die Widerlegung „FF01 ist nicht der Schlüssel" (2026-08-10) ist enger als sie
dasteht.** Getestet wurde `FC 55 40 41 68 7C 7F 5F` — die FF01 von Remote #2 im
**ungepaarten** Zustand. Der Post-Bind-Wert von Remote #2 wurde nie gelesen, und
laut PROTOCOL.md ändert sich der Wert über einen erfolgreichen Bind hinweg.
Belegt ist also: *der Wert vor dem Pairing ist nicht der Schlüssel.* Über den
Wert danach ist nichts bekannt.

**Mitschnitt B (Bind) ist damit die Priorität, nicht gestrichen.** Der Zweck ist
nicht, einen Schlüsselwert aus dem Trace zu ziehen, sondern die
**Nachrichtenfolge des Binds** zu lernen — und vor allem, was der bestätigende
Tastendruck **am Fan** auf die Leitung legt. Genau dieser Schritt fehlt für einen
ESPHome-seitigen Bind; alles andere funktioniert längst (Connect, Discovery,
Notify, Write auf FF02 — alles akzeptiert, 2026-08-10), und ESPHome spielt dabei
dieselbe Rolle wie das Originalmodul.

Kann ESPHome den Bind abschließen, löst sich die Schlüsselfrage möglicherweise
von selbst: erzeugt die **Fan-Seite** den Schlüssel, generiert ihn ein
ESPHome-Fan künftig selbst; liefert ihn die **Remote**, liest ESPHome ihn beim
Bind mit. Nur eine beidseitige Ableitung wäre echte Arbeit. In zwei von drei
Fällen ist die NVS-Voraussetzung weg.

Den Bind auszugeben kostet gerade nichts: sein einziger Wert war der `ble_key`
in Fan #3, und der ist unerreichbar.

- [ ] **Schritt 0: ist die Remote überhaupt noch gebunden?** 60 s aufnehmen, ein
      Mal *Power* drücken, Offset 9 ansehen. `status=0x02` → gebunden, dann lohnt
      Mitschnitt A **vor** B. Nur `0x01` → ungebunden, direkt zu B.
- [ ] **Mitschnitt B — der Bind.** Sniffer läuft vor dem ersten Tastendruck, das
      `CONNECT_IND` muss drin sein. Auszuwerten in dieser Reihenfolge: Schritt 3
      auf der Leitung · Richtung der Übergabe · FF01 vor und nach dem Bind · jeder
      8-Byte-Wert · SMP-Pakete:
      [`docs/nrf-sniffer-remote-capture.md`](docs/nrf-sniffer-remote-capture.md)
- [ ] **Danach:** neu binden und FF01 mit `bind_capture.yaml` auslesen. Ein
      Post-Bind-Wert für Remote #2 fehlt bis heute und braucht keinen Sniffer.
- [ ] Mitschnitt A (Tasten-Corpus mit Zustandsprotokoll) — nur falls B zeigt,
      dass der Schlüssel beidseitig abgeleitet wird. Sonst überflüssig.

Fällt der Schlüssel doch nicht ab, kommt die eingestellte **Lerntabelle** als
Rückfallebene zurück: verworfen wurde sie nur, weil die DES-Entschlüsselung sie
überflüssig machte. Die belegte ECB-Determiniertheit (gleicher Zielzustand →
identische acht Bytes) ist genau die Eigenschaft, die eine Match-Tabelle braucht.
Haken: die Payload trägt den **kompletten Zielzustand**, nicht eine Tasten-ID —
Einträge also pro erreichtem Zustand, nicht pro Taste.

Fürs nächste Mal: **ESPHome-Knoten vorher abschalten** — eine gehaltene
GATT-Verbindung bringt die Remote zum Schweigen. Mit abgeschaltetem BT-Proxy
sollte das erledigt sein; taucht `98:F4:AB:3C:9D:D6` trotzdem wieder auf, läuft
noch etwas. PC-Bluetooth und Logitech Bolt stören nur als Bandrauschen, nicht
inhaltlich — sie filtert die Adressfilterung weg.

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

- ~~Lerntabelle~~ — durch DES-Entschlüsselung überflüssig.
  **Als Rückfallebene wieder offen seit 2026-09-03,** falls sich ohne
  NVS-Zugriff kein Schlüssel beschaffen lässt. Siehe oben.
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
