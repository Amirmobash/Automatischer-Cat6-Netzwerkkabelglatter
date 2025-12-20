#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ==================== TFT wiring (ESP32 VSPI) ====================
#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_MISO  19   // optional
#define TFT_CS    15   // اگر جواب نداد 5 کن
#define TFT_DC     2
#define TFT_RST    4
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ==================== Pins ====================
#define JOY_Y   34
#define JOY_X   35
#define JOY_SW  27

#define PRESS_PIN 33   // ✅ ورودی ADC (بعد از تقسیم مقاومتی)

#define RELAY_PIN 25
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ==================== Screen ====================
#define SCREEN_W 240
#define SCREEN_H 320

// ==================== OLD palette (BGR numbers) -> swap R/B for Adafruit( RGB ) ====================
static inline uint16_t SWAP_RB(uint16_t c) {
  return (c & 0x07E0) | ((c & 0xF800) >> 11) | ((c & 0x001F) << 11);
}
#define ORANGE_BG_B     0x003F
#define ORANGE_DARK_B   0x02BF
#define ORANGE_MAIN_B   0x02DF
#define ORANGE_LIGHT_B  0x02FF
#define ORANGE_ACCENT_B 0x04FF
#define PANEL_BG_B      0x2104

#define ORANGE_BG     SWAP_RB(ORANGE_BG_B)
#define ORANGE_DARK   SWAP_RB(ORANGE_DARK_B)
#define ORANGE_MAIN   SWAP_RB(ORANGE_MAIN_B)
#define ORANGE_LIGHT  SWAP_RB(ORANGE_LIGHT_B)
#define ORANGE_ACCENT SWAP_RB(ORANGE_ACCENT_B)
#define PANEL_BG      SWAP_RB(PANEL_BG_B)

#define TEXT_COLOR    0xFFFF
#define BLACK_COLOR   0x0000
#define GREEN_COLOR   0x07E0
#define RED_COLOR     0xF800
#define LINE_COLOR    0x39E7

// ==================== Layout ====================
const int HEADER_H  = 44;
const int FOOTER_H  = 26;
const int CONTENT_Y = HEADER_H + 6;
const int CONTENT_H = SCREEN_H - HEADER_H - FOOTER_H - 12;

// ==================== Menu ====================
const char* menuItems[] = {" Sensor Mode", " Joystick Mode", " Auto Mode", " Druck Anzeige"};
const uint8_t menuCount = sizeof(menuItems) / sizeof(menuItems[0]);
uint8_t currentSelection = 0;
int8_t activeMode = -1; // -1 = menu

// ==================== Relay timing ====================
const uint16_t SENSOR_DURATION   = 10000;
const uint16_t JOYSTICK_DURATION = 4000;
const uint16_t AUTO_ON_TIME      = 5000;
const uint16_t AUTO_OFF_TIME     = 2000;

bool relayActive = false;
unsigned long relayTimer = 0;

// ==================== Joystick filter ====================
int centerX = 2048, centerY = 2048;
float fx = 2048, fy = 2048;
bool armedMenu = true;
bool armedJoy  = true;

const int DEADZONE = 220;
const int THRESH   = 520;
const float ALPHA  = 0.25f;
unsigned long lastMove = 0;

// ==================== Button click ====================
unsigned long lastButtonPress = 0;
unsigned long lastClickTime = 0;
uint8_t buttonClickCount = 0;

// ==================== Pressure sensor (Analog) ====================
// این سنسورها معمولاً: 0.5V..4.5V متناظر با 0..MAX_PRESSURE_KPA هستند.
const float VCC_SENSOR   = 5.00f;      // سنسور را با 5V تغذیه کردی
const float VMIN_SENSOR  = 0.50f;      // ولتاژ در فشار صفر
const float VMAX_SENSOR  = 4.50f;      // ولتاژ در فشار ماکس

// ✅ این را مطابق مدل سنسورت تنظیم کن (مثال‌ها):
//  - اگر 0..1.0MPa  => 1000 kPa
//  - اگر 0..0.7MPa  => 700 kPa
//  - اگر 0..1.6MPa  => 1600 kPa
float MAX_PRESSURE_KPA = 700.0f;

// تقسیم مقاومتی: 10k بالا + 20k پایین => Vadc = Vsensor * (20/(10+20)) = 0.6667
const float DIV_RATIO = 20.0f / (10.0f + 20.0f);  // 0.6667
// پس Vsensor = Vadc / DIV_RATIO

// فیلتر فشار
float pressureKpa = -1.0f;
float pressureFiltered = -1.0f;
const float PRESS_ALPHA = 0.18f;

// کالیبراسیون صفر (Offset) برای اینکه در هوای آزاد بشه ~0
float zeroOffsetKpa = 0.0f;

// OK Threshold قابل تنظیم (با چپ/راست در Pressure Mode)
float okThresholdKpa = 2.0f;     // ✅ برای فوت کردن/کم‌فشار (کم و زیادش کن)
const float OK_HYST = 0.5f;      // هیسترزیس برای جلوگیری از چشمک OK/NOT OK
bool okLatched = false;

// ==================== Auto mode ====================
bool autoState = false;
bool autoFirst = true;
unsigned long autoTimer = 0;

// ==================== Utils ====================
static inline int clampi(int v, int lo, int hi){ return v<lo?lo:(v>hi?hi:v); }
static inline float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }

void setTxt(uint8_t sz, uint16_t c) {
  tft.setTextSize(sz);
  tft.setTextColor(c);
}

void drawHeader(const char* title){
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, ORANGE_DARK);
  tft.drawFastHLine(0, HEADER_H-1, SCREEN_W, LINE_COLOR);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(10, 8);
  tft.print("Automatischer Cat6-Netzwerkkabelglätter");

  setTxt(1, ORANGE_LIGHT);
  tft.setCursor(10, 28);
  tft.print(title);

  // relay dot
  tft.fillRoundRect(SCREEN_W-40, 10, 30, 24, 10, PANEL_BG);
  tft.drawRoundRect(SCREEN_W-40, 10, 30, 24, 10, LINE_COLOR);
  tft.fillCircle(SCREEN_W-25, 22, 6, relayActive ? GREEN_COLOR : ORANGE_LIGHT);
}

void drawFooter(const char* text){
  tft.fillRect(0, SCREEN_H-FOOTER_H, SCREEN_W, FOOTER_H, ORANGE_DARK);
  tft.drawFastHLine(0, SCREEN_H-FOOTER_H, SCREEN_W, LINE_COLOR);
  setTxt(1, TEXT_COLOR);
  tft.setCursor(8, SCREEN_H-FOOTER_H+9);
  tft.print(text);
}

int itemY(uint8_t idx){
  const int cardH = 40;
  const int gap   = 10;
  return CONTENT_Y + 12 + idx * (cardH + gap);
}

void drawCard(int x,int y,int w,int h,bool sel){
  uint16_t fill = sel ? ORANGE_ACCENT : ORANGE_MAIN;
  uint16_t bord = sel ? ORANGE_LIGHT  : LINE_COLOR;
  tft.fillRoundRect(x, y, w, h, 10, fill);
  tft.drawRoundRect(x, y, w, h, 10, bord);
}

void drawMenuBase(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Menu");
  tft.fillRoundRect(10, CONTENT_Y, SCREEN_W-20, CONTENT_H, 14, PANEL_BG);
  tft.drawRoundRect(10, CONTENT_Y, SCREEN_W-20, CONTENT_H, 14, LINE_COLOR);
  drawFooter("UP/DOWN | 1:Enter | 3:Back");
}

void drawMenuItem(uint8_t i, bool selected){
  const int x = 18;
  const int w = SCREEN_W - 36;
  const int h = 40;
  int y = itemY(i);

  drawCard(x, y, w, h, selected);
  setTxt(2, selected ? BLACK_COLOR : TEXT_COLOR);
  tft.setCursor(x + 10, y + 12);
  tft.print(menuItems[i]);
}

void drawMainMenu(){
  drawMenuBase();
  for(uint8_t i=0;i<menuCount;i++){
    drawMenuItem(i, i==currentSelection);
  }
}

// ==================== Relay ====================
void activateRelay(uint16_t duration){
  digitalWrite(RELAY_PIN, RELAY_ON);
  relayActive = true;
  relayTimer = millis() + duration;
}
void checkRelayTimer(){
  if(relayActive && (long)(millis() - relayTimer) >= 0){
    digitalWrite(RELAY_PIN, RELAY_OFF);
    relayActive = false;
  }
}

// ==================== Joystick ====================
void calibrateJoyCenter(){
  long sx=0, sy=0;
  const int N=250;
  for(int i=0;i<N;i++){
    sx += analogRead(JOY_X);
    sy += analogRead(JOY_Y);
    delay(5);
  }
  centerX = sx/N;
  centerY = sy/N;
  fx = centerX;
  fy = centerY;
}
void updateJoyFiltered(){
  int rx = analogRead(JOY_X);
  int ry = analogRead(JOY_Y);
  fx = fx*(1.0f-ALPHA) + rx*ALPHA;
  fy = fy*(1.0f-ALPHA) + ry*ALPHA;
}
int joyDX(){ return (int)fx - centerX; }
int joyDY(){ return (int)fy - centerY; }

// ✅ منو: بالا = منو بالا / پایین = منو پایین
void handleMainMenuNav(){
  int dx = joyDX();
  int dy = joyDY();

  if (abs(dx) < DEADZONE && abs(dy) < DEADZONE) armedMenu = true;
  if (!armedMenu) return;
  if (millis() - lastMove < 160) return;

  uint8_t oldSel = currentSelection;

  if (dy < -THRESH) {
    // joystick UP -> menu UP
    currentSelection = (currentSelection==0)? (menuCount-1) : (currentSelection-1);
  } else if (dy > THRESH) {
    // joystick DOWN -> menu DOWN
    currentSelection = (currentSelection+1) % menuCount;
  } else {
    return;
  }

  drawMenuItem(oldSel, false);
  drawMenuItem(currentSelection, true);

  lastMove = millis();
  armedMenu = false;
}

// ==================== Button click & hold ====================
bool buttonPressedEdge(){
  bool pressed = (digitalRead(JOY_SW) == LOW);
  if(!pressed) return false;

  if(millis() - lastButtonPress > 220){
    if(millis() - lastClickTime < 500) buttonClickCount++;
    else buttonClickCount = 1;
    lastClickTime = millis();
    lastButtonPress = millis();
    return true;
  }
  return false;
}
bool tripleClickNow(){
  return (buttonClickCount >= 3 && (millis() - lastClickTime) < 800);
}

// نگه داشتن دکمه برای کالیبراسیون صفر فشار (Pressure Mode)
bool isButtonHeld(uint16_t msHold){
  static bool wasPressed = false;
  static unsigned long t0 = 0;

  bool pressed = (digitalRead(JOY_SW) == LOW);
  if (pressed && !wasPressed){
    wasPressed = true;
    t0 = millis();
  }
  if (!pressed && wasPressed){
    wasPressed = false;
  }
  if (pressed && wasPressed && (millis() - t0) > msHold){
    // یک بار تریگر
    while(digitalRead(JOY_SW)==LOW) delay(5);
    wasPressed = false;
    return true;
  }
  return false;
}

// ==================== Pressure read (Analog) ====================
float readAdcVoltage(int pin){
  // oversample average
  const int N = 40;
  long s = 0;
  for(int i=0;i<N;i++){
    s += analogRead(pin);
    delayMicroseconds(120);
  }
  float adc = (float)s / (float)N;   // 0..4095
  return (adc / 4095.0f) * 3.30f;    // ESP32 ADC reference ~3.3 (تقریب)
}

// تبدیل ولتاژ به kPa بر اساس 0.5..4.5V
float voltageToKpa(float Vsensor){
  float x = (Vsensor - VMIN_SENSOR) / (VMAX_SENSOR - VMIN_SENSOR); // 0..1
  x = clampf(x, 0.0f, 1.0f);
  return x * MAX_PRESSURE_KPA;
}

float readPressureKpa(){
  float Vadc = readAdcVoltage(PRESS_PIN);
  float Vsensor = Vadc / DIV_RATIO;             // برگرداندن تقسیم مقاومتی
  float kpa = voltageToKpa(Vsensor);
  return kpa - zeroOffsetKpa;                   // اعمال صفر کردن
}

void calibratePressureZero(){
  // چند ثانیه میانگین می‌گیریم
  tft.fillRect(14, CONTENT_Y+155, SCREEN_W-28, 36, ORANGE_ACCENT);
  tft.drawRoundRect(14, CONTENT_Y+155, SCREEN_W-28, 36, 10, ORANGE_LIGHT);
  setTxt(2, BLACK_COLOR);
  tft.setCursor(20, CONTENT_Y+167);
  tft.print("Zero Cal...");

  const int N = 80;
  float sum = 0;
  for(int i=0;i<N;i++){
    float Vadc = readAdcVoltage(PRESS_PIN);
    float Vsensor = Vadc / DIV_RATIO;
    sum += voltageToKpa(Vsensor);
    delay(20);
  }
  float avg = sum / N;
  zeroOffsetKpa = avg;  // حالا خروجی میشه avg-avg=0
}

// ==================== Mode UI ====================
void splashMode(const char* name){
  tft.fillScreen(ORANGE_BG);
  drawHeader(name);
  tft.fillRoundRect(18, CONTENT_Y+50, SCREEN_W-36, 80, 14, ORANGE_ACCENT);
  tft.drawRoundRect(18, CONTENT_Y+50, SCREEN_W-36, 80, 14, ORANGE_LIGHT);
  setTxt(2, BLACK_COLOR);
  tft.setCursor(28, CONTENT_Y+82);
  tft.print(name);
  delay(250);
}

// -------- Sensor Mode (اگر استفاده نمی‌کنی می‌تونی حذفش کنی) --------
#define SENSOR_TRIG_PIN 32   // اگر سنسور نوری نداری آزاد بذار
void drawSensorModeUI(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Sensor Mode");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, LINE_COLOR);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(26, CONTENT_Y+40);
  tft.print("Waiting signal");
  drawFooter("3-Clicks Back");
}
void runSensorMode(){
  static bool shown = false;

  if(digitalRead(SENSOR_TRIG_PIN) == HIGH && !relayActive){
    activateRelay(SENSOR_DURATION);
    shown = true;

    tft.fillRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, ORANGE_ACCENT);
    tft.drawRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, ORANGE_LIGHT);
    setTxt(2, BLACK_COLOR);
    tft.setCursor(52, CONTENT_Y+134);
    tft.print("ACTIVATED!");
  }

  checkRelayTimer();
  if(!relayActive && shown){
    shown = false;
    drawSensorModeUI();
  }
}

// -------- Joystick Mode (هر حرکت از مرکز => فعال‌سازی) --------
const int JOY_TRIG_RADIUS = 700;
void drawJoystickModeUI(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Joystick Mode");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, LINE_COLOR);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(30, CONTENT_Y+30);
  tft.print("Move joystick");
  tft.setCursor(30, CONTENT_Y+55);
  tft.print("to activate");

  drawFooter("3-Clicks Back");
}
void runJoystickMode(){
  static unsigned long lastTrig = 0;
  static bool wasActive = false;

  int dx = joyDX();
  int dy = joyDY();

  if (abs(dx) < DEADZONE && abs(dy) < DEADZONE) armedJoy = true;

  long dist2 = (long)dx*dx + (long)dy*dy;
  long r2 = (long)JOY_TRIG_RADIUS * JOY_TRIG_RADIUS;

  if(armedJoy && !relayActive && millis()-lastTrig > 500 && dist2 > r2){
    activateRelay(JOYSTICK_DURATION);
    lastTrig = millis();
    wasActive = true;
    armedJoy = false;

    tft.fillRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, ORANGE_ACCENT);
    tft.drawRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, ORANGE_LIGHT);
    setTxt(2, BLACK_COLOR);
    tft.setCursor(78, CONTENT_Y+134);
    tft.print("ACTIVE!");
  }

  checkRelayTimer();
  if(!relayActive && wasActive){
    wasActive = false;
    drawJoystickModeUI();
  }
}

// -------- Auto Mode --------
void drawAutoModeUI(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Auto Mode");

  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, LINE_COLOR);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(44, CONTENT_Y+30);
  tft.print("AUTO PULSE");

  setTxt(1, TEXT_COLOR);
  tft.setCursor(52, CONTENT_Y+58);
  tft.print("ON 5s / OFF 2s");

  drawFooter("3-Clicks Back (only OFF)");
}
void runAutoMode(){
  if(autoFirst){
    autoFirst = false;
    autoState = false;
    relayActive = false;
    digitalWrite(RELAY_PIN, RELAY_OFF);
    autoTimer = millis();
    drawAutoModeUI();
  }

  unsigned long now = millis();
  uint16_t interval = autoState ? AUTO_ON_TIME : AUTO_OFF_TIME;

  if(now - autoTimer >= interval){
    autoState = !autoState;
    autoTimer = now;

    digitalWrite(RELAY_PIN, autoState ? RELAY_ON : RELAY_OFF);
    relayActive = autoState;

    uint16_t badge = autoState ? ORANGE_ACCENT : ORANGE_DARK;
    tft.fillRoundRect(18, CONTENT_Y+95, SCREEN_W-36, 50, 14, badge);
    tft.drawRoundRect(18, CONTENT_Y+95, SCREEN_W-36, 50, 14, ORANGE_LIGHT);
    setTxt(2, autoState ? BLACK_COLOR : ORANGE_LIGHT);
    tft.setCursor(62, CONTENT_Y+112);
    tft.print(autoState ? "POWER ON" : "POWER OFF");
  }
}

// -------- Pressure Mode --------
void drawPressureUI(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Druck Anzeige");

  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W-28, 210, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W-28, 210, 14, LINE_COLOR);

  drawFooter("Hold=Zero | L/R=Thresh | 3=Back");
}

void drawPressureGauge(){
  // فقط داخل پنل پاک میشه (بدون چشمک کل صفحه)
  tft.fillRoundRect(18, CONTENT_Y+6, SCREEN_W-36, 198, 12, PANEL_BG);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(24, CONTENT_Y+18);
  tft.print("Pressure (kPa)");

  setTxt(2, ORANGE_LIGHT);
  tft.setCursor(24, CONTENT_Y+48);
  tft.print(pressureFiltered, 2);
  tft.print(" kPa");

  setTxt(1, TEXT_COLOR);
  tft.setCursor(24, CONTENT_Y+78);
  tft.print("ZeroOffset: ");
  tft.print(zeroOffsetKpa, 1);

  tft.setCursor(24, CONTENT_Y+92);
  tft.print("Threshold: ");
  tft.print(okThresholdKpa, 1);

  // latch with hysteresis
  float p = pressureFiltered;
  if (p < (okThresholdKpa - OK_HYST)) okLatched = false;
  if (p > (okThresholdKpa + OK_HYST)) okLatched = true;

  uint16_t sc = okLatched ? GREEN_COLOR : RED_COLOR;
  const char* s = okLatched ? "OK" : "NOT OK";

  tft.fillRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, sc);
  tft.drawRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, ORANGE_LIGHT);
  setTxt(2, BLACK_COLOR);
  tft.setCursor(80, CONTENT_Y+134);
  tft.print(s);

  // bar visualization (0..max)
  int barX = 24, barY = CONTENT_Y+175, barW = 192, barH = 14;
  tft.drawRect(barX, barY, barW, barH, LINE_COLOR);
  tft.fillRect(barX+1, barY+1, barW-2, barH-2, BLACK_COLOR);

  float visMax = clampf(max(okThresholdKpa*2.0f, 20.0f), 20.0f, 400.0f);
  float cp = clampf(p, 0.0f, visMax);
  int fw = (int)((cp/visMax)*(barW-2));
  fw = clampi(fw, 0, barW-2);
  tft.fillRect(barX+1, barY+1, fw, barH-2, sc);
}

void runPressureMode(){
  static unsigned long lastUpdate = 0;

  // چپ/راست برای تغییر threshold
  int dx = joyDX();
  if (abs(dx) < DEADZONE) {
    // nothing
  } else {
    if (millis() - lastMove > 180) {
      if (dx > THRESH) okThresholdKpa += 0.5f;      // right => threshold بیشتر
      if (dx < -THRESH) okThresholdKpa -= 0.5f;     // left  => threshold کمتر
      okThresholdKpa = clampf(okThresholdKpa, 0.0f, 200.0f);
      lastMove = millis();
    }
  }

  // نگه داشتن دکمه = صفر کردن
  if (isButtonHeld(1200)) {
    calibratePressureZero();
  }

  if (millis() - lastUpdate > 180){
    pressureKpa = readPressureKpa();
    if (pressureFiltered < 0) pressureFiltered = pressureKpa;
    pressureFiltered = pressureFiltered*(1.0f-PRESS_ALPHA) + pressureKpa*PRESS_ALPHA;
    drawPressureGauge();
    lastUpdate = millis();
  }
}

// ==================== Setup / Loop ====================
void setup(){
  Serial.begin(115200);

  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  pinMode(SENSOR_TRIG_PIN, INPUT);  // اگر استفاده نمی‌کنی هم مشکلی نیست

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X, ADC_11db);
  analogSetPinAttenuation(JOY_Y, ADC_11db);
  analogSetPinAttenuation(PRESS_PIN, ADC_11db);

  // TFT
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(2); // ✅ 180 درجه

  // Calibrate joystick center
  tft.fillScreen(BLACK_COLOR);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 70);
  tft.print("Calibrating...");
  tft.setTextSize(1);
  tft.setCursor(10, 98);
  tft.print("Keep joystick centered!");
  calibrateJoyCenter();

  // یک بار هم صفر فشار رو بگیر (اختیاری)
  tft.fillScreen(BLACK_COLOR);
  tft.setCursor(10, 70);
  tft.setTextSize(2);
  tft.print("Zero pressure...");
  delay(300);
  calibratePressureZero();

  drawMainMenu();
}

void loop(){
  updateJoyFiltered();

  if(buttonPressedEdge()){
    if(activeMode == -1){
      if(buttonClickCount == 1){
        activeMode = currentSelection;
        splashMode(menuItems[currentSelection]);

        if(activeMode == 0) drawSensorModeUI();
        if(activeMode == 1) { armedJoy = true; drawJoystickModeUI(); }
        if(activeMode == 2) { autoFirst = true; drawAutoModeUI(); }
        if(activeMode == 3) { drawPressureUI(); drawPressureGauge(); }
      }
    } else {
      if(tripleClickNow()){
        if(activeMode != 2 || !relayActive){
          digitalWrite(RELAY_PIN, RELAY_OFF);
          relayActive = false;
          activeMode = -1;
          buttonClickCount = 0;
          drawMainMenu();
          return;
        }
      }
    }
  }

  if(activeMode == -1){
    handleMainMenuNav();
  } else {
    switch(activeMode){
      case 0: runSensorMode();   break;
      case 1: runJoystickMode(); break;
      case 2: runAutoMode();     break;
      case 3: runPressureMode(); break;
    }
  }

  checkRelayTimer();
}
