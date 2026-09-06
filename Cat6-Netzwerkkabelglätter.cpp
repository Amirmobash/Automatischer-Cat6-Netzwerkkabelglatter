#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ============================================================
// Hardware configuration
// ESP32 + ILI9341 + analog joystick + pressure sensor + relay
// ============================================================

constexpr uint8_t TFT_SCK  = 18;
constexpr uint8_t TFT_MOSI = 23;
constexpr uint8_t TFT_MISO = 19;
constexpr uint8_t TFT_CS   = 15;  // If your board wiring uses GPIO 5, change only this constant.
constexpr uint8_t TFT_DC   = 2;
constexpr uint8_t TFT_RST  = 4;

constexpr uint8_t JOY_Y  = 34;
constexpr uint8_t JOY_X  = 35;
constexpr uint8_t JOY_SW = 27;

constexpr uint8_t PRESS_PIN       = 33;
constexpr uint8_t SENSOR_TRIG_PIN = 32;
constexpr uint8_t RELAY_PIN       = 25;

constexpr uint8_t RELAY_ON  = LOW;   // Active-low relay module.
constexpr uint8_t RELAY_OFF = HIGH;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ============================================================
// Display configuration
// ============================================================

constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 320;
constexpr int16_t HEADER_H = 44;
constexpr int16_t FOOTER_H = 26;
constexpr int16_t CONTENT_Y = HEADER_H + 6;
constexpr int16_t CONTENT_H = SCREEN_H - HEADER_H - FOOTER_H - 12;

static inline uint16_t swapRedBlue(uint16_t color) {
  return (color & 0x07E0U) |
         ((color & 0xF800U) >> 11U) |
         ((color & 0x001FU) << 11U);
}

constexpr uint16_t ORANGE_BG_B     = 0x003F;
constexpr uint16_t ORANGE_DARK_B   = 0x02BF;
constexpr uint16_t ORANGE_MAIN_B   = 0x02DF;
constexpr uint16_t ORANGE_LIGHT_B  = 0x02FF;
constexpr uint16_t ORANGE_ACCENT_B = 0x04FF;
constexpr uint16_t PANEL_BG_B      = 0x2104;

const uint16_t ORANGE_BG     = swapRedBlue(ORANGE_BG_B);
const uint16_t ORANGE_DARK   = swapRedBlue(ORANGE_DARK_B);
const uint16_t ORANGE_MAIN   = swapRedBlue(ORANGE_MAIN_B);
const uint16_t ORANGE_LIGHT  = swapRedBlue(ORANGE_LIGHT_B);
const uint16_t ORANGE_ACCENT = swapRedBlue(ORANGE_ACCENT_B);
const uint16_t PANEL_BG      = swapRedBlue(PANEL_BG_B);

constexpr uint16_t TEXT_COLOR  = 0xFFFF;
constexpr uint16_t BLACK_COLOR = 0x0000;
constexpr uint16_t GREEN_COLOR = 0x07E0;
constexpr uint16_t RED_COLOR   = 0xF800;
constexpr uint16_t LINE_COLOR  = 0x39E7;

// ============================================================
// Application modes
// ============================================================

enum class Mode : int8_t {
  Menu = -1,
  Sensor = 0,
  Joystick = 1,
  Auto = 2,
  Pressure = 3,
};

const char* const MENU_ITEMS[] = {
  " Sensor-Modus",
  " Joystick-Modus",
  " Auto-Modus",
  " Druck Anzeige",
};

constexpr uint8_t MENU_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
uint8_t currentSelection = 0;
Mode activeMode = Mode::Menu;

// ============================================================
// Timing and control constants
// ============================================================

constexpr uint32_t SENSOR_DURATION_MS   = 10000UL;
constexpr uint32_t JOYSTICK_DURATION_MS = 4000UL;
constexpr uint32_t AUTO_ON_TIME_MS      = 5000UL;
constexpr uint32_t AUTO_OFF_TIME_MS     = 2000UL;

constexpr uint32_t BUTTON_DEBOUNCE_MS    = 30UL;
constexpr uint32_t MULTI_CLICK_GAP_MS    = 550UL;
constexpr uint32_t PRESS_LONG_HOLD_MS    = 1200UL;
constexpr uint32_t MENU_MOVE_GUARD_MS    = 160UL;
constexpr uint32_t THRESHOLD_STEP_MS     = 180UL;
constexpr uint32_t PRESSURE_UI_PERIOD_MS = 180UL;

static inline bool elapsedAtLeast(uint32_t now, uint32_t since, uint32_t interval) {
  return static_cast<uint32_t>(now - since) >= interval;
}

static inline bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

static inline int clampInt(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

static inline float clampFloat(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

// ============================================================
// Relay state
// Timed relay control is intentionally separated from Auto mode.
// This prevents a stale timed-relay deadline from switching Auto off.
// ============================================================

bool relayActive = false;
bool timedRelayRunning = false;
uint32_t timedRelayDeadline = 0;
bool displayReady = false;

void drawRelayIndicator();

void setRelayOutput(bool on) {
  relayActive = on;
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);

  if (displayReady) {
    drawRelayIndicator();
  }
}

void stopRelay() {
  timedRelayRunning = false;
  setRelayOutput(false);
}

void startTimedRelay(uint32_t durationMs) {
  timedRelayRunning = true;
  timedRelayDeadline = millis() + durationMs;
  setRelayOutput(true);
}

void serviceTimedRelay(uint32_t now) {
  if (!timedRelayRunning) {
    return;
  }

  if (deadlineReached(now, timedRelayDeadline)) {
    timedRelayRunning = false;
    setRelayOutput(false);
  }
}

// ============================================================
// Joystick state
// ============================================================

constexpr int JOY_DEFAULT_CENTER = 2048;
constexpr int JOY_DEADZONE = 220;
constexpr int JOY_THRESHOLD = 520;
constexpr int JOY_TRIGGER_RADIUS = 700;
constexpr float JOY_FILTER_ALPHA = 0.25f;

int centerX = JOY_DEFAULT_CENTER;
int centerY = JOY_DEFAULT_CENTER;
float filteredJoyX = JOY_DEFAULT_CENTER;
float filteredJoyY = JOY_DEFAULT_CENTER;

bool menuArmed = true;
bool joystickModeArmed = true;
uint32_t lastMenuMoveAt = 0;
uint32_t lastThresholdStepAt = 0;

void calibrateJoystickCenter() {
  constexpr int SAMPLE_COUNT = 250;
  int64_t sumX = 0;
  int64_t sumY = 0;

  for (int i = 0; i < SAMPLE_COUNT; ++i) {
    sumX += analogRead(JOY_X);
    sumY += analogRead(JOY_Y);
    delay(5);
  }

  centerX = static_cast<int>(sumX / SAMPLE_COUNT);
  centerY = static_cast<int>(sumY / SAMPLE_COUNT);
  filteredJoyX = static_cast<float>(centerX);
  filteredJoyY = static_cast<float>(centerY);
}

void updateJoystickFilter() {
  const int rawX = analogRead(JOY_X);
  const int rawY = analogRead(JOY_Y);

  filteredJoyX = filteredJoyX * (1.0f - JOY_FILTER_ALPHA) +
                 static_cast<float>(rawX) * JOY_FILTER_ALPHA;
  filteredJoyY = filteredJoyY * (1.0f - JOY_FILTER_ALPHA) +
                 static_cast<float>(rawY) * JOY_FILTER_ALPHA;
}

int joystickDx() {
  return static_cast<int>(filteredJoyX) - centerX;
}

int joystickDy() {
  return static_cast<int>(filteredJoyY) - centerY;
}

// ============================================================
// Button state machine
// Uses real debounced state transitions instead of treating a held
// button as repeated clicks.
// ============================================================

enum class ButtonEvent : uint8_t {
  None,
  Click,
  TripleClick,
  LongPress,
};

struct ButtonState {
  bool rawPressed = false;
  bool stablePressed = false;
  bool longPressFired = false;
  uint32_t rawChangedAt = 0;
  uint32_t pressedAt = 0;
  uint32_t lastClickAt = 0;
  uint8_t clickCount = 0;
};

ButtonState button;

void resetButtonClickSequence() {
  button.clickCount = 0;
  button.lastClickAt = 0;
}

ButtonEvent updateButton(uint32_t now, bool allowLongPress) {
  const bool rawPressed = digitalRead(JOY_SW) == LOW;

  if (rawPressed != button.rawPressed) {
    button.rawPressed = rawPressed;
    button.rawChangedAt = now;
  }

  if (button.rawPressed != button.stablePressed &&
      elapsedAtLeast(now, button.rawChangedAt, BUTTON_DEBOUNCE_MS)) {
    button.stablePressed = button.rawPressed;

    if (button.stablePressed) {
      button.pressedAt = now;
      button.longPressFired = false;
    } else {
      if (!button.longPressFired) {
        if (button.clickCount > 0 &&
            elapsedAtLeast(now, button.lastClickAt, MULTI_CLICK_GAP_MS)) {
          button.clickCount = 0;
        }

        ++button.clickCount;
        button.lastClickAt = now;

        if (button.clickCount >= 3) {
          resetButtonClickSequence();
          return ButtonEvent::TripleClick;
        }

        return ButtonEvent::Click;
      }
    }
  }

  if (allowLongPress &&
      button.stablePressed &&
      !button.longPressFired &&
      elapsedAtLeast(now, button.pressedAt, PRESS_LONG_HOLD_MS)) {
    button.longPressFired = true;
    resetButtonClickSequence();
    return ButtonEvent::LongPress;
  }

  if (!button.stablePressed &&
      button.clickCount > 0 &&
      elapsedAtLeast(now, button.lastClickAt, MULTI_CLICK_GAP_MS)) {
    resetButtonClickSequence();
  }

  return ButtonEvent::None;
}

// ============================================================
// Pressure sensor
// Sensor assumption: 0.5 V ... 4.5 V maps to 0 ... MAX_PRESSURE_KPA.
// Divider assumption: 10k high-side + 20k low-side => Vadc = Vsensor * 2/3.
// ============================================================

constexpr float SENSOR_MIN_VOLTAGE = 0.50f;
constexpr float SENSOR_MAX_VOLTAGE = 4.50f;
constexpr float MAX_PRESSURE_KPA = 700.0f;
constexpr float DIVIDER_RATIO = 20.0f / 30.0f;
constexpr float PRESSURE_FILTER_ALPHA = 0.18f;
constexpr float OK_HYSTERESIS_KPA = 0.5f;

float pressureKpa = 0.0f;
float filteredPressureKpa = 0.0f;
float pressureZeroOffsetKpa = 0.0f;
float okThresholdKpa = 2.0f;
bool pressureFilterInitialized = false;
bool pressureOkLatched = false;

float readAdcVoltage(uint8_t pin) {
  // analogReadMilliVolts() uses the ESP32 ADC calibration path when supported
  // by the installed Arduino-ESP32 core and is preferable to assuming 3.300 V.
  constexpr uint8_t SAMPLE_COUNT = 32;
  uint32_t sumMillivolts = 0;

  for (uint8_t i = 0; i < SAMPLE_COUNT; ++i) {
    sumMillivolts += static_cast<uint32_t>(analogReadMilliVolts(pin));
    delayMicroseconds(120);
  }

  const float averageMillivolts =
      static_cast<float>(sumMillivolts) / static_cast<float>(SAMPLE_COUNT);
  return averageMillivolts / 1000.0f;
}

float sensorVoltageToKpa(float sensorVoltage) {
  const float normalized = clampFloat(
      (sensorVoltage - SENSOR_MIN_VOLTAGE) /
          (SENSOR_MAX_VOLTAGE - SENSOR_MIN_VOLTAGE),
      0.0f,
      1.0f);

  return normalized * MAX_PRESSURE_KPA;
}

float readUncalibratedPressureKpa() {
  const float adcVoltage = readAdcVoltage(PRESS_PIN);
  const float sensorVoltage = adcVoltage / DIVIDER_RATIO;
  return sensorVoltageToKpa(sensorVoltage);
}

float readPressureKpa() {
  // This sensor is treated as a positive-pressure sensor. Small negative values
  // after zero calibration are therefore clamped to zero.
  return max(0.0f, readUncalibratedPressureKpa() - pressureZeroOffsetKpa);
}

// ============================================================
// Auto mode state
// ============================================================

bool autoRelayOn = false;
uint32_t autoPhaseStartedAt = 0;

// ============================================================
// Sensor/joystick mode UI state
// ============================================================

bool sensorArmed = false;
bool sensorShowingActive = false;
bool joystickShowingActive = false;
uint32_t joystickLastTriggerAt = 0;
uint32_t pressureLastUpdateAt = 0;

// ============================================================
// Drawing helpers
// ============================================================

void setText(uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
}

int16_t textWidth(const char* text, uint8_t size) {
  return static_cast<int16_t>(strlen(text) * 6U * size);
}

void drawCenteredText(const char* text, int16_t y, uint8_t size, uint16_t color) {
  setText(size, color);
  const int16_t calculatedX = (SCREEN_W - textWidth(text, size)) / 2;
  const int16_t x = calculatedX > 0 ? calculatedX : 0;
  tft.setCursor(x, y);
  tft.print(text);
}

void drawRelayIndicator() {
  tft.fillRoundRect(SCREEN_W - 40, 10, 30, 24, 10, PANEL_BG);
  tft.drawRoundRect(SCREEN_W - 40, 10, 30, 24, 10, LINE_COLOR);
  tft.fillCircle(SCREEN_W - 25, 22, 6,
                 relayActive ? GREEN_COLOR : ORANGE_LIGHT);
}

void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, ORANGE_DARK);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, LINE_COLOR);

  setText(2, TEXT_COLOR);
  tft.setCursor(10, 6);
  tft.print("CAT6 Glaetter");

  setText(1, ORANGE_LIGHT);
  tft.setCursor(10, 29);
  tft.print(title);

  drawRelayIndicator();
}

void drawFooter(const char* text) {
  tft.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, ORANGE_DARK);
  tft.drawFastHLine(0, SCREEN_H - FOOTER_H, SCREEN_W, LINE_COLOR);
  setText(1, TEXT_COLOR);
  tft.setCursor(8, SCREEN_H - FOOTER_H + 9);
  tft.print(text);
}

int16_t menuItemY(uint8_t index) {
  constexpr int16_t CARD_H = 40;
  constexpr int16_t GAP = 10;
  return CONTENT_Y + 12 + index * (CARD_H + GAP);
}

void drawCard(int16_t x, int16_t y, int16_t width, int16_t height, bool selected) {
  const uint16_t fill = selected ? ORANGE_ACCENT : ORANGE_MAIN;
  const uint16_t border = selected ? ORANGE_LIGHT : LINE_COLOR;
  tft.fillRoundRect(x, y, width, height, 10, fill);
  tft.drawRoundRect(x, y, width, height, 10, border);
}

void drawMenuItem(uint8_t index, bool selected) {
  constexpr int16_t X = 18;
  constexpr int16_t WIDTH = SCREEN_W - 36;
  constexpr int16_t HEIGHT = 40;
  const int16_t y = menuItemY(index);

  drawCard(X, y, WIDTH, HEIGHT, selected);
  setText(2, selected ? BLACK_COLOR : TEXT_COLOR);
  tft.setCursor(X + 10, y + 12);
  tft.print(MENU_ITEMS[index]);
}

void drawMainMenu() {
  tft.fillScreen(ORANGE_BG);
  drawHeader("Menue");
  tft.fillRoundRect(10, CONTENT_Y, SCREEN_W - 20, CONTENT_H, 14, PANEL_BG);
  tft.drawRoundRect(10, CONTENT_Y, SCREEN_W - 20, CONTENT_H, 14, LINE_COLOR);

  for (uint8_t i = 0; i < MENU_COUNT; ++i) {
    drawMenuItem(i, i == currentSelection);
  }

  drawFooter("Hoch/Runter | Klick=OK");
}

void drawSensorModeUI() {
  tft.fillScreen(ORANGE_BG);
  drawHeader("Sensor-Modus");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);

  drawCenteredText("Warte auf Signal", CONTENT_Y + 40, 2, TEXT_COLOR);
  drawFooter("3x Klick = zurueck");
}

void drawSensorActiveBadge() {
  tft.fillRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_ACCENT);
  tft.drawRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
  drawCenteredText("AKTIVIERT!", CONTENT_Y + 134, 2, BLACK_COLOR);
}

void drawJoystickModeUI() {
  tft.fillScreen(ORANGE_BG);
  drawHeader("Joystick-Modus");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);

  drawCenteredText("Bewege Joystick", CONTENT_Y + 30, 2, TEXT_COLOR);
  drawCenteredText("zum Aktivieren", CONTENT_Y + 55, 2, TEXT_COLOR);
  drawFooter("3x Klick = zurueck");
}

void drawJoystickActiveBadge() {
  tft.fillRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_ACCENT);
  tft.drawRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
  drawCenteredText("AKTIV!", CONTENT_Y + 134, 2, BLACK_COLOR);
}

void drawAutoModeUI() {
  tft.fillScreen(ORANGE_BG);
  drawHeader("Auto-Modus");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);

  drawCenteredText("AUTO-IMPULS", CONTENT_Y + 30, 2, TEXT_COLOR);
  drawCenteredText("EIN 5s / AUS 2s", CONTENT_Y + 58, 1, TEXT_COLOR);
  drawFooter("3x zurueck (nur AUS)");
}

void drawAutoStateBadge() {
  const uint16_t badgeColor = autoRelayOn ? ORANGE_ACCENT : ORANGE_DARK;
  const uint16_t textColor = autoRelayOn ? BLACK_COLOR : ORANGE_LIGHT;

  tft.fillRoundRect(18, CONTENT_Y + 95, SCREEN_W - 36, 50, 14, badgeColor);
  tft.drawRoundRect(18, CONTENT_Y + 95, SCREEN_W - 36, 50, 14, ORANGE_LIGHT);
  drawCenteredText(autoRelayOn ? "EIN" : "AUS", CONTENT_Y + 112, 2, textColor);
}

void drawPressureUI() {
  tft.fillScreen(ORANGE_BG);
  drawHeader("Druck Anzeige");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 210, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 210, 14, LINE_COLOR);
  drawFooter("Halten=Null L/R=Limit 3x=Zurueck");
}

void updatePressureLatch(float pressure) {
  if (pressure < okThresholdKpa - OK_HYSTERESIS_KPA) {
    pressureOkLatched = false;
  } else if (pressure > okThresholdKpa + OK_HYSTERESIS_KPA) {
    pressureOkLatched = true;
  }
}

void drawPressureGauge() {
  tft.fillRoundRect(18, CONTENT_Y + 6, SCREEN_W - 36, 198, 12, PANEL_BG);

  setText(2, TEXT_COLOR);
  tft.setCursor(24, CONTENT_Y + 18);
  tft.print("Druck (kPa)");

  setText(2, ORANGE_LIGHT);
  tft.setCursor(24, CONTENT_Y + 48);
  if (pressureFilterInitialized) {
    tft.print(filteredPressureKpa, 2);
    tft.print(" kPa");
  } else {
    tft.print("--.-- kPa");
  }

  setText(1, TEXT_COLOR);
  tft.setCursor(24, CONTENT_Y + 78);
  tft.print("Nulloffset: ");
  tft.print(pressureZeroOffsetKpa, 1);

  tft.setCursor(24, CONTENT_Y + 92);
  tft.print("Schwellwert: ");
  tft.print(okThresholdKpa, 1);

  const float pressure = pressureFilterInitialized ? filteredPressureKpa : 0.0f;
  updatePressureLatch(pressure);

  const uint16_t statusColor = pressureOkLatched ? GREEN_COLOR : RED_COLOR;
  const char* statusText = pressureOkLatched ? "OK" : "NICHT OK";

  tft.fillRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, statusColor);
  tft.drawRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
  drawCenteredText(statusText, CONTENT_Y + 134, 2, BLACK_COLOR);

  constexpr int16_t BAR_X = 24;
  constexpr int16_t BAR_Y = CONTENT_Y + 175;
  constexpr int16_t BAR_W = 192;
  constexpr int16_t BAR_H = 14;

  tft.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, LINE_COLOR);
  tft.fillRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, BLACK_COLOR);

  const float visualMax = clampFloat(max(okThresholdKpa * 2.0f, 20.0f),
                                     20.0f,
                                     400.0f);
  const float clampedPressure = clampFloat(pressure, 0.0f, visualMax);
  int fillWidth = static_cast<int>((clampedPressure / visualMax) * (BAR_W - 2));
  fillWidth = clampInt(fillWidth, 0, BAR_W - 2);

  if (fillWidth > 0) {
    tft.fillRect(BAR_X + 1, BAR_Y + 1, fillWidth, BAR_H - 2, statusColor);
  }
}

void drawCalibrationMessage() {
  tft.fillRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_ACCENT);
  tft.drawRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
  drawCenteredText("Null kalibrieren", CONTENT_Y + 134, 1, BLACK_COLOR);
}

// ============================================================
// Pressure calibration
// ============================================================

void calibratePressureZero(bool showUi) {
  if (showUi && displayReady) {
    drawCalibrationMessage();
  }

  constexpr uint8_t CALIBRATION_SAMPLES = 60;
  float sumKpa = 0.0f;

  for (uint8_t i = 0; i < CALIBRATION_SAMPLES; ++i) {
    sumKpa += readUncalibratedPressureKpa();
    delay(20);
  }

  pressureZeroOffsetKpa = sumKpa / static_cast<float>(CALIBRATION_SAMPLES);
  pressureKpa = 0.0f;
  filteredPressureKpa = 0.0f;
  pressureFilterInitialized = true;
  pressureOkLatched = false;

  Serial.print("Pressure zero offset: ");
  Serial.print(pressureZeroOffsetKpa, 2);
  Serial.println(" kPa");
}

// ============================================================
// Mode transitions
// ============================================================

void enterMode(Mode mode) {
  activeMode = mode;
  resetButtonClickSequence();

  switch (mode) {
    case Mode::Sensor:
      stopRelay();
      sensorShowingActive = false;
      // If the input is already high on entry, wait until it returns low before
      // accepting a new rising condition. This avoids immediate/repeated firing.
      sensorArmed = digitalRead(SENSOR_TRIG_PIN) == LOW;
      drawSensorModeUI();
      break;

    case Mode::Joystick:
      stopRelay();
      joystickModeArmed = true;
      joystickShowingActive = false;
      joystickLastTriggerAt = millis() - 500UL;
      drawJoystickModeUI();
      break;

    case Mode::Auto:
      stopRelay();
      autoRelayOn = false;
      autoPhaseStartedAt = millis();
      drawAutoModeUI();
      drawAutoStateBadge();
      break;

    case Mode::Pressure:
      stopRelay();
      lastThresholdStepAt = 0;
      pressureLastUpdateAt = 0;
      drawPressureUI();
      drawPressureGauge();
      break;

    case Mode::Menu:
      drawMainMenu();
      break;
  }
}

void exitToMenu() {
  stopRelay();
  autoRelayOn = false;
  activeMode = Mode::Menu;
  resetButtonClickSequence();
  drawMainMenu();
}

// ============================================================
// Mode runners
// ============================================================

void handleMainMenuNavigation(uint32_t now) {
  const int dx = joystickDx();
  const int dy = joystickDy();

  if (abs(dx) < JOY_DEADZONE && abs(dy) < JOY_DEADZONE) {
    menuArmed = true;
  }

  if (!menuArmed || !elapsedAtLeast(now, lastMenuMoveAt, MENU_MOVE_GUARD_MS)) {
    return;
  }

  const uint8_t previous = currentSelection;

  if (dy < -JOY_THRESHOLD) {
    currentSelection = currentSelection == 0 ? MENU_COUNT - 1 : currentSelection - 1;
  } else if (dy > JOY_THRESHOLD) {
    currentSelection = (currentSelection + 1) % MENU_COUNT;
  } else {
    return;
  }

  drawMenuItem(previous, false);
  drawMenuItem(currentSelection, true);
  lastMenuMoveAt = now;
  menuArmed = false;
}

void runSensorMode(uint32_t now) {
  const bool sensorHigh = digitalRead(SENSOR_TRIG_PIN) == HIGH;

  if (!sensorHigh) {
    sensorArmed = true;
  }

  if (sensorHigh && sensorArmed && !timedRelayRunning) {
    sensorArmed = false;
    sensorShowingActive = true;
    startTimedRelay(SENSOR_DURATION_MS);
    drawSensorActiveBadge();
  }

  serviceTimedRelay(now);

  if (!timedRelayRunning && sensorShowingActive) {
    sensorShowingActive = false;
    drawSensorModeUI();
  }
}

void runJoystickMode(uint32_t now) {
  const int dx = joystickDx();
  const int dy = joystickDy();

  if (abs(dx) < JOY_DEADZONE && abs(dy) < JOY_DEADZONE) {
    joystickModeArmed = true;
  }

  const int64_t distanceSquared =
      static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy;
  constexpr int64_t TRIGGER_RADIUS_SQUARED =
      static_cast<int64_t>(JOY_TRIGGER_RADIUS) * JOY_TRIGGER_RADIUS;

  if (joystickModeArmed &&
      !timedRelayRunning &&
      elapsedAtLeast(now, joystickLastTriggerAt, 500UL) &&
      distanceSquared > TRIGGER_RADIUS_SQUARED) {
    joystickModeArmed = false;
    joystickShowingActive = true;
    joystickLastTriggerAt = now;
    startTimedRelay(JOYSTICK_DURATION_MS);
    drawJoystickActiveBadge();
  }

  serviceTimedRelay(now);

  if (!timedRelayRunning && joystickShowingActive) {
    joystickShowingActive = false;
    drawJoystickModeUI();
  }
}

void runAutoMode(uint32_t now) {
  const uint32_t interval = autoRelayOn ? AUTO_ON_TIME_MS : AUTO_OFF_TIME_MS;

  if (!elapsedAtLeast(now, autoPhaseStartedAt, interval)) {
    return;
  }

  autoRelayOn = !autoRelayOn;
  autoPhaseStartedAt = now;
  timedRelayRunning = false;
  setRelayOutput(autoRelayOn);
  drawAutoStateBadge();
}

void runPressureMode(uint32_t now) {
  const int dx = joystickDx();

  if (elapsedAtLeast(now, lastThresholdStepAt, THRESHOLD_STEP_MS)) {
    bool changed = false;

    if (dx > JOY_THRESHOLD) {
      okThresholdKpa += 0.5f;
      changed = true;
    } else if (dx < -JOY_THRESHOLD) {
      okThresholdKpa -= 0.5f;
      changed = true;
    }

    if (changed) {
      okThresholdKpa = clampFloat(okThresholdKpa, 0.0f, 200.0f);
      lastThresholdStepAt = now;
      drawPressureGauge();
    }
  }

  if (!elapsedAtLeast(now, pressureLastUpdateAt, PRESSURE_UI_PERIOD_MS)) {
    return;
  }

  pressureKpa = readPressureKpa();

  if (!pressureFilterInitialized) {
    filteredPressureKpa = pressureKpa;
    pressureFilterInitialized = true;
  } else {
    filteredPressureKpa =
        filteredPressureKpa * (1.0f - PRESSURE_FILTER_ALPHA) +
        pressureKpa * PRESSURE_FILTER_ALPHA;
  }

  drawPressureGauge();
  pressureLastUpdateAt = now;
}

// ============================================================
// Setup / loop
// ============================================================

void setup() {
  Serial.begin(115200);

  pinMode(JOY_SW, INPUT_PULLUP);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  relayActive = false;
  timedRelayRunning = false;

  // Active-high trigger. INPUT_PULLDOWN prevents a disconnected input from
  // floating high and unexpectedly energizing the relay.
  pinMode(SENSOR_TRIG_PIN, INPUT_PULLDOWN);

  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X, ADC_11db);
  analogSetPinAttenuation(JOY_Y, ADC_11db);
  analogSetPinAttenuation(PRESS_PIN, ADC_11db);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(2);
  displayReady = true;

  tft.fillScreen(BLACK_COLOR);
  setText(2, TEXT_COLOR);
  tft.setCursor(10, 70);
  tft.print("Kalibriere...");
  setText(1, TEXT_COLOR);
  tft.setCursor(10, 98);
  tft.print("Joystick mittig halten!");
  calibrateJoystickCenter();

  tft.fillScreen(BLACK_COLOR);
  setText(2, TEXT_COLOR);
  tft.setCursor(10, 70);
  tft.print("Druck nullen...");
  setText(1, TEXT_COLOR);
  tft.setCursor(10, 98);
  tft.print("Sensor druckfrei lassen");
  delay(250);
  calibratePressureZero(false);

  Serial.println("CAT6 cable straightener ready.");
  drawMainMenu();
}

void loop() {
  const uint32_t now = millis();
  updateJoystickFilter();

  const bool allowLongPress = activeMode == Mode::Pressure;
  const ButtonEvent buttonEvent = updateButton(now, allowLongPress);

  if (activeMode == Mode::Menu) {
    if (buttonEvent == ButtonEvent::Click) {
      enterMode(static_cast<Mode>(currentSelection));
      return;
    }

    handleMainMenuNavigation(now);
    return;
  }

  if (buttonEvent == ButtonEvent::TripleClick) {
    // Preserve the original safety rule: Auto mode may only be left while
    // the relay is OFF. Other modes are stopped immediately on exit.
    if (activeMode != Mode::Auto || !relayActive) {
      exitToMenu();
      return;
    }
  }

  if (activeMode == Mode::Pressure && buttonEvent == ButtonEvent::LongPress) {
    calibratePressureZero(true);
    drawPressureUI();
    drawPressureGauge();
    pressureLastUpdateAt = millis();
    return;
  }

  switch (activeMode) {
    case Mode::Sensor:
      runSensorMode(now);
      break;

    case Mode::Joystick:
      runJoystickMode(now);
      break;

    case Mode::Auto:
      runAutoMode(now);
      break;

    case Mode::Pressure:
      runPressureMode(now);
      break;

    case Mode::Menu:
      break;
  }
}
