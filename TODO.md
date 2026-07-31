# DreamMaker Fan — Open Tasks

> **Richtungswechsel 2026-07-31** (aus Handoff der Parallel-Session):
> Tasten werden **als BLE-Advertisement** gesendet (`status=0x02`, 8-Byte
> Payload), **nicht** über GATT. Die alte "Beacon ausgeschlossen"-Annahme war an
> einer *ungebundenen* Remote gemessen und ist widerlegt. Siehe PROTOCOL.md.

---

## 🔴 Prio 1: Beacon-Lerntabelle aufbauen (neuer Hauptweg)

Die Zuordnung *Zielzustand → 8-Byte-Payload* ist **deterministisch** (bewiesen
durch 4 Wiederholungen im Capture). Deshalb ist **keine Entschlüsselung nötig** —
eine Lerntabelle reicht.

- [ ] Remote an einen Fan binden (sonst nur Idle-Heartbeats!)
      - Fan: *Head-shaking + Timer* → 4 LEDs blinken
      - Remote: *Power + M* → 8 LEDs blinken
      - Taste am Fan → Bind + Bestätigungston
- [ ] ESPHome-Fan als **passiven Scanner** danebenstellen (`ble_remote: true`)
- [ ] Jede Aktion einzeln auslösen, Payload + resultierenden Fan-Zustand notieren:
      - Power ⏻ (kurz) → On/Off
      - M kurz → Speed-Cycle (4 Stufen = 4 Payloads)
      - M lang → Modus-Cycle (Direct/Natural/Smart = 3 Payloads)
      - Head-shaking ∿ → Oszillation On/Off
      - Clock 🕐 → Timer-Cycle (0/1/2/3/4h = 5 Payloads)
- [ ] Tabelle in PROTOCOL.md eintragen (Payload ↔ Zielzustand)
- [ ] Prüfen: liefert **unsere** Remote (`4B:F2:7E:47:E5:6E`) dieselben Chiffrate
      wie im alten 23-Payload-Capture? Wenn ja → Tabelle direkt wiederverwendbar

### Offene Frage zur Architektur

Die Lerntabelle ist **pro Remote** (an den Bond gekoppelt). Für andere Nutzer
braucht es einen **Learn-Mode** in der Komponente statt fest kodierter Werte.
→ Design-Entscheidung nötig, bevor Phase 3 implementiert wird.

---

## 🟡 Prio 2: `0x1F44`-Fix verifizieren (Code bereits geändert)

`on_action1_()` sendete `data_len=0`, die echte Firmware sendet
`data_len=1, data=[0x01]` ("agree to pair"). Fix ist eingebaut, **noch nicht
auf Hardware getestet**.

- [ ] Flashen und am Fan *Head-shaking + Timer* halten
- [ ] Log prüfen: `MCU remote-pairing trigger (0x1F44) → ACK agree=1`
- [ ] Prüfen ob der Fan jetzt tatsächlich in den Pairing-Modus geht
      (vorher evtl. blockiert durch das unvollständige ACK)
- [ ] Falls ja: das war ein echter Blocker für das gesamte Pairing über ESPHome

---

## 🟢 Prio 3: GATT-Weg — Status klären

Der Challenge-Echo (FF01→FF02) **funktioniert** (Blinken stoppt), liefert aber
keine Tastenevents. Vermutlich Teil des *Bind*-Protokolls, nicht des Kommandowegs.

- [ ] Entscheiden: `ble_capture.yaml` weiter pflegen oder archivieren?
- [ ] Falls Beacon-Weg (Prio 1) funktioniert → GATT nur noch fürs Binden nutzen

---

## 🔵 Prio 4: Phase 3 — dm_fan.h Implementierung

(erst wenn Lerntabelle steht)

- [ ] Beacon-Handler: `status=0x02` → Payload gegen Lerntabelle matchen
- [ ] Gematchten Zustand auf die Fan-Entities anwenden
- [ ] Learn-Mode: Payload + aktueller Fan-Zustand speichern (NVS/`restore_value`)
- [ ] `ble_report_to_mcu` überdenken — brauchen wir den 0x1F41-Weg überhaupt,
      wenn wir den Zustand direkt selbst setzen?
- [ ] Tag `v4.0.0` (stable)

---

## ⚪ Prio 5: Hardware-Wege (beide bisher ergebnislos)

- [ ] **Remote SWD**: ST-Link meldet `chipid: 0x000`, auch mit
      `--connect-under-reset` → Debug-Port vermutlich gesperrt.
      Verkabelungsfehler nicht 100 % ausgeschlossen (Spannung/Kontinuität
      nicht durchgemessen). Niedrige Priorität, da Beacon-Weg trägt.
- [ ] **Remote UART**: nur Rauschen mit lesbaren Fragmenten, kein
      validiertes Frame extrahiert. Offene Hypothese: RTS/CTS-Flusskontrolle.
- [ ] **Fan-MCU SWD**: noch nicht versucht — würde `0x1F41`-Format bestätigen

---

## 📝 Prio 6: Offene Doku-/Community-Aufgaben

- [ ] **Korrektur in `dhewg/esphome-miot#50` posten**: unser alter Kommentar vom
      19. Mai behauptet `action:81 / resource:0x70` für die WiFi-Query-Antwort.
      Das war Spekulation aus dem Binary und ist **falsch** — korrekt ist
      `action:82 / resource:0x78` (echot die Query-Resource).
      Nachtrag-Kommentar, alten nicht löschen.
- [ ] Flash-Backup Hälfte 2 (`0x200000`–`0x400000`) des gepairten Fans
      nachziehen — Hälfte 1 ist sauber gesichert

---

## 🔒 Sicherheitshinweis

Aus dem gepairten NVS-Dump stammen **echte Geräte-Secrets** (`ble_key`,
`device_key`, `device_id`, WiFi-Zugangsdaten). Diese sind **bewusst nicht** in
diesem öffentlichen Repo dokumentiert — nur Struktur und Fundort. Lokal halten.
