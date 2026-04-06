# Automatischer Cat6-Netzwerkkabelglätter

Ein ESP32‑gesteuertes Gerät zum automatischen Richten von Cat6‑Netzwerkkabeln mittels pneumatischem Druck. Die Steuerung erfolgt über ein Farb‑TFT‑Display (ILI9341) und einen Joystick. Das Herzstück ist ein Relais, das eine Druckluft‑ oder Hydraulikvorrichtung ansteuert.

[![Aufbau]([https://via.placeholder.com/800x400?text=Bild+folgt](https://github.com/Amirmobash/Automatischer-Cat6-Netzwerkkabelglatter/blob/main/amir%20AC_U_.jpg?raw=true))](https://github.com/Amirmobash/Automatischer-Cat6-Netzwerkkabelglatter/blob/main/amir%20AC_U_.jpg?raw=true)  
*Platzhalter für ein Foto des Aufbaus*

---

## 📦 Features

- **4 Betriebsmodi** (auswählbar über Menü)
  - **Sensor‑Modus** – Startet bei externem Triggersignal (z. B. Lichtschranke)
  - **Joystick‑Modus** – Aktivierung durch wegführen des Joysticks aus der Mitte
  - **Auto‑Modus** – Automatischer 5s‑Ein / 2s‑Aus Takt
  - **Druck‑Anzeige** – Misst und zeigt den aktuellen Pneumatikdruck an (kPa)
- **Farbiges ILI9341‑Display** (240×320) mit benutzerdefinierter Oberfläche
- **Joystick‑Navigation** mit Entprellung und Totzone
- **Relaisausgang** (potentialfrei) zur Ansteuerung eines Magnetventils
- **Drucksensor** (0,5‑4,5 V) mit Spannungsteiler (10 kΩ / 20 kΩ)
- **Nullpunkt‑Kalibrierung** für den Drucksensor per Tastendruck
- **Einstellbarer OK‑Schwellwert** (links/rechts im Druckmodus)
- **Hysterese** gegen Flackern der OK‑Anzeige

---

## 🔧 Hardware‑Anforderungen

| Komponente               | Typ / Wert                         |
|--------------------------|------------------------------------|
| Mikrocontroller          | ESP32 (z. B. NodeMCU‑32S)         |
| Display                  | ILI9341 (SPI, 240×320)             |
| Joystick                 | 2‑Achser + Taster (analog)         |
| Relais                   | 5 V Relais Modul (Low‑Aktiv)       |
| Drucksensor (0‑700 kPa) | z. B. mit 0,5‑4,5 V Ausgang        |
| Spannungsteiler          | 10 kΩ (oben) / 20 kΩ (unten)       |
| Netzteil                 | 5 V / 2 A (für ESP32 und Relais)   |
| Sonstiges                | Kabel, Steckbrett, Gehäuse         |

> **Hinweis:** Der Drucksensor wird mit 5 V versorgt, sein Ausgang über den Spannungsteiler auf ESP32‑kompatible 0‑3,3 V reduziert.

---

## 📌 Pinbelegung (ESP32)

| Funktion          | GPIO‑Pin |
|-------------------|----------|
| TFT_SCK (CLK)     | 18       |
| TFT_MOSI (DIN)    | 23       |
| TFT_MISO          | 19 (optional) |
| TFT_CS            | 15       |
| TFT_DC            | 2        |
| TFT_RST           | 4        |
| JOY_X (Analog)    | 35       |
| JOY_Y (Analog)    | 34       |
| JOY_SW (Taster)   | 27       |
| PRESS_PIN (ADC)   | 33       |
| RELAY_PIN         | 25       |
| SENSOR_TRIG_PIN*  | 32       |

> *Nur im Sensor‑Modus benötigt (z. B. Lichtschranke HIGH‑Aktiv). Falls nicht verwendet, bleibt der Pin offen.

---

## 🚀 Erste Schritte

### 1. Software installieren
- **Arduino IDE** (oder PlatformIO) mit ESP32‑Unterstützung
- Installiere folgende Bibliotheken:
  - `Adafruit GFX Library`
  - `Adafruit ILI9341`
  - `SPI` (bereits enthalten)

### 2. Code anpassen
Öffne die Datei `Cat6-Netzwerkkabelglätter.cpp` und prüfe / ändere folgende Konstanten:

```cpp
// Drucksensorbereich (laut Datenblatt)
MAX_PRESSURE_KPA = 700.0f;   // z. B. 0..0,7 MPa → 700 kPa

// Spannungsteiler (falls andere Widerstände)
const float DIV_RATIO = 20.0f / (10.0f + 20.0f); // 0,6667

// TFT‑Rotation (je nach Einbaulage)
tft.setRotation(2);
```

### 3. Hochladen
- Verbinde den ESP32 über USB mit dem PC
- Wähle das richtige Board (z. B. *NodeMCU-32S*) und den COM‑Port
- Klicke auf **Hochladen**

Nach dem ersten Start kalibriert das Programm die Joystick‑Mitte und den Druck‑Nullpunkt (frei Luft). Danach erscheint das Hauptmenü.

---

## 🕹️ Bedienung

### Hauptmenü
- **Joystick nach oben / unten** → Menüpunkt auswählen
- **Joystick kurz drücken (1‑fach Klick)** → gewählten Modus starten
- **Joystick dreimal kurz drücken** (innerhalb 0,8 s) → im aktiven Modus zurück zum Menü

### Sensor‑Modus
- Das Relais wird für 10 Sekunden aktiviert, sobald am Pin `SENSOR_TRIG_PIN` HIGH anliegt.
- Die Anzeige zeigt `AKTIVIERT!`.
- Rückkehr zum Menü: **3‑fach Klick**

### Joystick‑Modus
- Bewege den Joystick weit aus der Mittelstellung (Radius > 700).
- Das Relais schaltet für 4 Sekunden.
- Danach kann sofort wieder ausgelöst werden.
- Rückkehr: **3‑fach Klick**

### Auto‑Modus
- Startet sofort mit dem Takt: **5 Sekunden EIN – 2 Sekunden AUS** – endlos.
- Das Relais blinkt entsprechend.
- Zum Beenden **3‑fach Klick** (während der AUS‑Phase, um das Relais auszuschalten).

### Druck‑Anzeige
- Zeigt den aktuellen Pneumatikdruck in kPa an.
- **Joystick links / rechts** → verändert den OK‑Schwellwert (0 … 200 kPa).
- **Joystick gedrückt halten (≥ 1,2 Sekunden)** → Nullpunktkalibrierung (Druck auf 0 setzen).
- Die Anzeige wechselt zwischen **OK** (grün) und **NICHT OK** (rot) abhängig vom Schwellwert + Hysterese.
- Ein Balkendiagramm visualisiert den Druck relativ zum doppelten Schwellwert (max. 400 kPa).
- Rückkehr: **3‑fach Klick**

---

## ⚙️ Kalibrierung

### Joystick‑Mitte
Wird automatisch beim Start durchgeführt.  
**Wichtig:** Den Joystick während der Kalibrierung **nicht** berühren (die Meldung `Joystick in Mittelstellung halten!` beachten).

### Druck‑Nullpunkt
Ebenfalls automatisch beim Start (Sensoreingang offen / kein Druck).  
Im laufenden Betrieb kann der Nullpunkt jederzeit durch **langes Drücken** des Joysticks im **Druck‑Anzeige**‑Modus neu gesetzt werden.

> Der Nullpunkt wird als fester Offset gespeichert – ein erneutes Kalibrieren überschreibt den vorherigen Wert.

---

## 🔩 Elektrischer Anschluss (Skizze)

```
ESP32              ILI9341 TFT
─────              ───────────
GPIO18  ────────►  SCK (CLK)
GPIO23  ────────►  MOSI (DIN)
GPIO19  ◄────────  MISO (optional)
GPIO15  ────────►  CS
GPIO2   ────────►  DC
GPIO4   ────────►  RESET

ESP32              Joystick Modul
─────              ─────────────
GPIO35  ◄────────  VRx (X‑Achse)
GPIO34  ◄────────  VRy (Y‑Achse)
GPIO27  ◄────────  SW (Taster)
3.3V    ─────────  VCC (je nach Modul)
GND     ─────────  GND

ESP32              Drucksensor (0,5‑4,5V)
─────              ──────────────────────
GPIO33  ◄────────  Ausgang ──┬── 10kΩ ── 5V
                     (Vadc)   │
                              └── 20kΩ ── GND
5V      ─────────  Versorgung (+)
GND     ─────────  Masse (-)

ESP32              Relais Modul
─────              ────────────
GPIO25  ────────►  IN (Low‑Aktiv)
5V      ─────────  VCC
GND     ─────────  GND

Relais‑Kontakte   →  Magnetventil (230V / 24V, je nach Last)
```

---

## 🛠️ Fehlersuche

| Problem                                  | Mögliche Lösung |
|------------------------------------------|----------------|
| Display bleibt schwarz                   | Prüfe SPI‑Verkabelung, setze `TFT_CS` auf Pin 5 (falls nötig) |
| Joystick reagiert nicht                  | Kalibrierung wiederholen (Neustart). Totzone/Threshold anpassen |
| Druckanzeige schwankt stark              | Filterzeitkonstante `PRESS_ALPHA` erhöhen (z. B. 0,05) |
| Nullpunktkalibrierung funktioniert nicht | Sicherstellen, dass der Sensor wirklich drucklos ist. |
| Relais schaltet nicht                    | Prüfe `RELAY_ON` / `RELAY_OFF` (Low‑aktiv). Externe Spannung am Relais? |
| 3‑fach Klick wird nicht erkannt          | Entprellzeit (`lastButtonPress`) oder Zeitfenster (`800` ms) anpassen |

---

## 📄 Lizenz

Dieses Projekt wird unter der **MIT‑Lizenz** veröffentlicht – siehe Datei `LICENSE`.

---

## 🙏 Danksagung

- Bibliotheken: Adafruit GFX, Adafruit ILI9341
- Inspiration: Verschiedene Open‑Source‑Pneumatiksteuerungen

---

## 🔮 Ausblick / Erweiterungsmöglichkeiten

- Speichern des Nullpunkts im EEPROM
- Echtzeit‑Diagramm des Druckverlaufs
- Unterstützung weiterer Drucksensoren (I²C)
- Fernsteuerung via Webinterface (ESP32 als Webserver)
- Akkubetrieb mit Tiefschlafmodus

---

**Viel Erfolg beim Bau deines eigenen Cat6‑Kabelglätters!**  
Bei Fragen → Issue auf GitHub eröffnen.
```
