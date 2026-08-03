# DreamMaker Fan — Open Tasks

> **Durchbruch 2026-08-03:** Die 8-Byte-Beacon-Payload ist **DES-ECB** mit dem
> `ble_key` aus dem NVS. Vollständig dekodiert inkl. Prüfsumme — 18/18 Payloads
> konsistent. Lerntabelle wird nicht mehr gebraucht. Siehe PROTOCOL.md.

---

## 🟢 Prio 1: ✅ AUF HARDWARE BESTÄTIGT (2026-08-03)

Die Fernbedienung steuert den Fan. Alle fünf Tasten getestet, keine einzige
`checksum mismatch`-Zeile:

| Taste | getestet |
|-------|----------|
| Power | ✅ an/aus |
| Speed | ✅ voller Zyklus 35→70→100→1→35 |
| Mode | ✅ direct→natural→smart |
| Oszillation | ✅ an/aus mehrfach |
| Timer | ✅ 0→60→120→180→240 min |

Scan-Fenster `200ms/100ms` reicht — jeder Tastendruck kam an.

### 🔍 Beobachtung zum Nachprüfen: Speed-Anzeige im Smart-Modus

Im Smart-Modus meldet die MCU eine selbst geregelte Drehzahl (`spd=50%`),
während die Fernbedienung ihren Zielwert schickt (`speed=100`). HA zeigt 100.

Ursache: der Anti-Flap-Guard (`STALE_GUARD_MS = 250`) verwirft MCU-Frames, die
kurz nach einem eigenen Kommando kommen — hier ist das aber keine Echo-Dopplung,
sondern echte Information. Im Testlog folgte jeder MCU-Frame binnen ~40-90 ms auf
einen Tastendruck, deshalb war nie ein ungesperrter Frame zu sehen.

- [ ] Prüfen: gleicht sich die Anzeige nach ein paar Sekunden Ruhe von selbst an?
- [ ] Falls nein: Guard so verfeinern, dass er nur echte Echos verwirft
      (z. B. Vergleich gegen den zuletzt gesendeten Wert statt reiner Zeitfenster)

### ✅ Erledigt (2026-08-03)

- [x] Config-Option `ble_key` (8 Byte Hex, akzeptiert Leerzeichen/Doppelpunkte)
- [x] DES **selbst implementiert** (`components/dm_fan/des.h`) statt mbedTLS —
      `MBEDTLS_DES_C` ist in ESP-IDF standardmäßig AUS, eine sdkconfig-
      Abhängigkeit wäre eine Fehlerquelle für jeden Nutzer
      - verifiziert gegen FIPS-46-3-Vektor, Roundtrip und alle Capture-Payloads
        (byte-identisch zu pycryptodome)
- [x] Payload dekodieren + Prüfsumme validieren
- [x] Ungültige Prüfsumme → verwerfen (Schutz vor fremden Fernbedienungen)
- [x] Zusätzlich Wertebereiche prüfen, bevor etwas auf den UART geht
- [x] Nur Deltas senden — sonst 5 UART-Frames pro Tastendruck statt 1
- [x] `ble_report_to_mcu` als bestätigte Sackgasse markiert

### Offene Design-Frage — woher bekommt der Nutzer den `ble_key`?

Aktuell nur aus dem NVS des **originalen** Fans. Wer schon geflasht hat, ohne zu
sichern, kommt nicht mehr dran.

**Vielversprechende Spur (unbestätigt):** Die ersten **8 Byte** der
GATT-Bind-Nachricht (FF01) sind genau schlüsselgroß, und die Neu-Gruppierung
`8|6|6` zeigt sie als einziges variables Feld (siehe PROTOCOL.md). Falls das der
Schlüssel ist, könnte ESPHome ihn beim eigenen Bind selbst lernen — dann bräuchte
niemand mehr einen NVS-Dump.

- [ ] **Entscheidender Test:** Fan mit Original-FW koppeln, dabei FF01
      mitschneiden, danach NVS dumpen und `ble_key` gegen die ersten 8 Byte
      vergleichen
- [ ] Falls Treffer: Bind in dm_fan.h implementieren, Schlüssel automatisch lernen
- [ ] Bis dahin: Anleitung zum NVS-Dump **vor** dem Flashen

**Ausgeschlossen:** Der Schlüssel kommt nicht aus der Cloud (Pairing-Mitschnitt
zeigt keinen Austausch) und ist nicht aus MAC/product_id/device_id/device_key
ableitbar (mehrere hundert Ableitungen getestet, kein Treffer).

---

## 🟡 Prio 2: `ble_key` sichern (falls noch nicht geschehen)

- [ ] NVS des gepairten Fans sichern, solange Original-FW noch drauf ist
      `esptool.py --port COMx read_flash 0x9000 0x4000 nvs_backup.bin`
- [ ] `ble_key` extrahieren (8-Byte-Blob; **Achtung:** bei `type=0x41`, `span=2`
      stehen die Daten am Anfang des FOLGENDEN 32-Byte-Blocks, nicht im
      Metadaten-Eintrag; NVS ist wear-levelled → Kopie mit echter CRC nehmen)
- [ ] Fernbedienung **nicht** neu koppeln (Power+M) — neuer Bond = neuer Key

---

## 🟢 Prio 3: Aufräumen / Verifikation

- [ ] Nach dem Einbau: alle 5 Tasten durchtesten, ob der Fan korrekt folgt
- [ ] Prüfen ob `byte[5]` (immer 0) doch etwas kodiert — z. B. Winkel?
      Im Capture wurde der Winkel nie über die Fernbedienung verstellt, und die
      Fernbedienung hat auch keine Winkel-Taste — vermutlich echt ungenutzt.
- [ ] `ble_capture.yaml` / `ble_discovery.yaml` archivieren — der GATT-Weg ist
      nicht mehr nötig (Challenge-Echo gehört zum Bind, nicht zum Kommandoweg)
- [ ] Tag `v4.0.0` (stable)

---

## ⚪ Prio 4: Nicht mehr nötig / eingestellt

- ~~Lerntabelle aufbauen~~ — durch DES-Entschlüsselung überflüssig
- ~~Rohes Beacon-Forwarding an die MCU~~ — Format bestätigt (MCU ACKt), aber
  wirkungslos: das ESP-Modul war die entschlüsselnde Seite, nicht die MCU
- **Remote SWD**: ST-Link meldet `chipid: 0x000` → Debug-Port vermutlich
  gesperrt. Nicht mehr nötig.
- **Remote UART**: nur Rauschen, kein validiertes Frame. Nicht mehr nötig.

---

## 📝 Prio 5: Doku / Community

- [ ] **Korrektur in `dhewg/esphome-miot#50` posten**: unser alter Kommentar vom
      19. Mai behauptet `action:81 / resource:0x70` für die WiFi-Query-Antwort.
      Korrekt ist `action:82 / resource:0x78` (echot die Query-Resource).
- [ ] README: BLE-Fernbedienung als Feature dokumentieren, inkl. der
      Einschränkung, dass der `ble_key` vorher gesichert werden muss
- [ ] Flash-Backup Hälfte 2 (`0x200000`–`0x400000`) nachziehen

---

## 🔒 Sicherheitshinweis

`ble_key`, `device_key`, `device_id` und WiFi-Zugangsdaten sind **pro Gerät
geheim** und stehen **nicht** in diesem öffentlichen Repo — nur Struktur und
Fundort. Der `ble_key` gehört in `secrets.yaml`, nicht in die Gerätekonfiguration.
