#include <Arduino.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <Wire.h>

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
Preferences prefs;

// --- Mapeamento de Pinos (ESP32-C3) ---
const int PIN_BTN_UP    = 5;
const int PIN_BTN_DOWN  = 6;
const int PIN_BTN_ENTER = 7;

// --- Máquina de Estados ---
enum MainScreens { LAP_TIME, DELTA, SPEED, SETTINGS };
enum LapModes    { CUR, BEST, LAST };
enum GapModes    { G_BEST, G_LAST, G_OPT };

MainScreens currentScreen  = LAP_TIME;
LapModes    currentLapMode = CUR;
GapModes    currentGapMode = G_BEST;

// Estado anterior para detectar mudanças e evitar full redraws desnecessários
MainScreens prevScreen   = (MainScreens)255;
LapModes    prevLapMode  = (LapModes)255;
GapModes    prevGapMode  = (GapModes)255;
bool        prevBlinkOn  = false;

// --- Variáveis de Configuração e Controle ---
int brightness = 128;
bool editMode = false;
unsigned long lastButtonPress = 0;
const int debounceDelay = 200;

// --- Variáveis de Telemetria (C-Strings para otimização de memória) ---
// Formato esperado do SimHub: CUR_LAP;BEST_LAP;LAST_LAP;GAP_BEST;GAP_LAST;GAP_OPT;SPEED\n
char curLap[10]  = "0:00.00";
char bstLap[10]  = "0:00.00";
char lstLap[10]  = "0:00.00";
char gBest[8]    = "0.00";
char gLast[8]    = "0.00";
char gOpt[8]     = "0.00";
char speedStr[4] = "0";

// --- Buffer para recepção Serial ---
char serialBuffer[64];
int bufferIndex = 0;

// --- Layout de tiles (128x64 = 16 colunas x 8 linhas de 8x8px cada) ---
// Header: pixel y=0..15  → tile rows 0-1 (label estático)
// Content: pixel y=16..63 → tile rows 2-7 (valores dinâmicos)
static const uint8_t CONTENT_Y  = 16;
static const uint8_t CONTENT_H  = 48;
static const uint8_t HEADER_TY  = 0;
static const uint8_t HEADER_TH  = 2;
static const uint8_t CONTENT_TY = 2;
static const uint8_t CONTENT_TH = 6;
static const uint8_t TILE_COLS  = 16;

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);

  prefs.begin("racelogic", false);
  currentScreen  = (MainScreens)prefs.getUChar("screen",     LAP_TIME);
  currentLapMode = (LapModes)  prefs.getUChar("lapMode",    CUR);
  currentGapMode = (GapModes)  prefs.getUChar("gapMode",    G_BEST);
  brightness     =             prefs.getInt(  "brightness",  128);

  Wire.begin(8, 9);
  Wire.setClock(400000);
  u8g2.begin();
  u8g2.setContrast(brightness);
  pinMode(PIN_BTN_UP,    INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN,  INPUT_PULLUP);
  pinMode(PIN_BTN_ENTER, INPUT_PULLUP);
}

void handleButtons() {
  if (millis() - lastButtonPress < debounceDelay) return;

  bool btnUp    = (digitalRead(PIN_BTN_UP)    == LOW);
  bool btnDown  = (digitalRead(PIN_BTN_DOWN)  == LOW);
  bool btnEnter = (digitalRead(PIN_BTN_ENTER) == LOW);

  if (!(btnUp || btnDown || btnEnter)) return;
  lastButtonPress = millis();

  if (editMode) {
    if (btnUp   && brightness < 255) { brightness += 15; if (brightness > 255) brightness = 255; }
    if (btnDown && brightness > 0)   { brightness -= 15; if (brightness < 0)   brightness = 0;   }
    if (btnEnter) {
      editMode = false;
      prefs.putInt("brightness", brightness);
    }
    u8g2.setContrast(brightness);
    return;
  }

  if (btnUp) {
    currentScreen = static_cast<MainScreens>((currentScreen + 1) % 4);
    prefs.putUChar("screen", currentScreen);
  }
  if (btnDown) {
    currentScreen = static_cast<MainScreens>((currentScreen == 0) ? 3 : currentScreen - 1);
    prefs.putUChar("screen", currentScreen);
  }

  if (btnEnter) {
    if (currentScreen == LAP_TIME) {
      currentLapMode = static_cast<LapModes>((currentLapMode + 1) % 3);
      prefs.putUChar("lapMode", currentLapMode);
    } else if (currentScreen == DELTA) {
      currentGapMode = static_cast<GapModes>((currentGapMode + 1) % 3);
      prefs.putUChar("gapMode", currentGapMode);
    } else if (currentScreen == SETTINGS) {
      editMode = true;
    }
  }
}

void processSerialData() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (bufferIndex == 0) continue; // ignora \n do par \r\n
      serialBuffer[bufferIndex] = '\0';
      char *ptr = strtok(serialBuffer, ";");
      if (ptr != NULL) { strncpy(curLap,   ptr, sizeof(curLap)   - 1); ptr = strtok(NULL, ";"); }
      if (ptr != NULL) { strncpy(bstLap,   ptr, sizeof(bstLap)   - 1); ptr = strtok(NULL, ";"); }
      if (ptr != NULL) { strncpy(lstLap,   ptr, sizeof(lstLap)   - 1); ptr = strtok(NULL, ";"); }
      if (ptr != NULL) { strncpy(gBest,    ptr, sizeof(gBest)    - 1); ptr = strtok(NULL, ";"); }
      if (ptr != NULL) { strncpy(gLast,    ptr, sizeof(gLast)    - 1); ptr = strtok(NULL, ";"); }
      if (ptr != NULL) { strncpy(gOpt,     ptr, sizeof(gOpt)     - 1); ptr = strtok(NULL, ";"); }
      if (ptr != NULL) { strncpy(speedStr, ptr, sizeof(speedStr) - 1); }
      bufferIndex = 0;
    } else if (bufferIndex < (int)sizeof(serialBuffer) - 1) {
      serialBuffer[bufferIndex++] = c;
    }
  }
}

// --- Header (y=0..15): label estático, redesenhado só quando a tela/modo muda ---
void drawHeader() {
  u8g2.setFont(u8g2_font_helvR08_tr);
  switch (currentScreen) {
    case LAP_TIME:
      u8g2.drawStr(0, 10,
        currentLapMode == CUR  ? "CURRENT LAP" :
        currentLapMode == BEST ? "BEST LAP"    : "LAST LAP");
      break;
    case DELTA:
      u8g2.drawStr(0, 10,
        currentGapMode == G_BEST ? "GAP BEST"    :
        currentGapMode == G_LAST ? "GAP LAST"    : "GAP OPTIMAL");
      break;
    case SPEED:
      u8g2.drawStr(0, 10, "SPEED KM/H");
      break;
    case SETTINGS:
      u8g2.drawStr(0, 10, "BRIGHTNESS");
      if (editMode && (millis() / 500) % 2 == 0)
        u8g2.drawStr(95, 10, "[EDIT]");
      break;
  }
}

// --- Content (y=16..63): valores dinâmicos, redesenhado todo frame ---
// Posicionamento garante que o topo dos glifos fique em y>=18 (tile row 2),
// evitando sobreposição com a área de header nos updates parciais.
void drawContent() {
  switch (currentScreen) {
    case LAP_TIME: {
      char* val = currentLapMode == CUR  ? curLap :
                  currentLapMode == BEST ? bstLap  : lstLap;
      u8g2.setFont(u8g2_font_logisoso28_tn);
      u8g2.drawStr(5, 46, val);
      break;
    }
    case DELTA: {
      char* val = currentGapMode == G_BEST ? gBest :
                  currentGapMode == G_LAST ? gLast  : gOpt;
      u8g2.setFont(u8g2_font_logisoso24_tr);
      u8g2.drawStr(28, 40, val);

      float gVal = atof(val);
      int barWidth = (int)(fabsf(gVal) / 2.0f * 64.0f);
      if (barWidth > 64) barWidth = 64;
      u8g2.drawLine(64, 50, 64, 63);
      if      (gVal < 0.0f) u8g2.drawBox(64 - barWidth, 54, barWidth, 9);
      else if (gVal > 0.0f) u8g2.drawBox(64,             54, barWidth, 9);
      break;
    }
    case SPEED:
      u8g2.setFont(u8g2_font_logisoso42_tn);
      u8g2.drawStr(25, 58, speedStr);
      break;
    case SETTINGS: {
      int barWidth = map(brightness, 0, 255, 0, 110);
      u8g2.drawFrame(5, 30, 114, 15);
      u8g2.drawBox(7, 32, barWidth, 11);
      u8g2.setCursor(50, 60);
      u8g2.print(map(brightness, 0, 255, 0, 100));
      u8g2.print("%");
      break;
    }
  }
}

bool needsFullRedraw() {
  return currentScreen  != prevScreen  ||
         currentLapMode != prevLapMode ||
         currentGapMode != prevGapMode;
}

void updatePrevState() {
  prevScreen   = currentScreen;
  prevLapMode  = currentLapMode;
  prevGapMode  = currentGapMode;
}

void debugData() {
  if(false) {
    static unsigned long lastDebugPrint = 0;
    if (millis() - lastDebugPrint < 500) return;
    lastDebugPrint = millis();

    Serial.println("=== DEBUG ===");
    Serial.print("Screen: ");        Serial.println(currentScreen);
    Serial.print("LapMode: ");       Serial.println(currentLapMode);
    Serial.print("GapMode: ");       Serial.println(currentGapMode);
    Serial.print("EditMode: ");      Serial.println(editMode);
    Serial.print("Brightness: ");    Serial.println(brightness);
    Serial.print("curLap: ");        Serial.println(curLap);
    Serial.print("bstLap: ");        Serial.println(bstLap);
    Serial.print("lstLap: ");        Serial.println(lstLap);
    Serial.print("gBest: ");         Serial.println(gBest);
    Serial.print("gLast: ");         Serial.println(gLast);
    Serial.print("gOpt: ");          Serial.println(gOpt);
    Serial.print("speedStr: ");      Serial.println(speedStr);
    Serial.print("prevScreen: ");    Serial.println(prevScreen);
    Serial.print("prevLapMode: ");   Serial.println(prevLapMode);
    Serial.print("prevGapMode: ");   Serial.println(prevGapMode);
    Serial.print("needsFullRedraw: "); Serial.println(needsFullRedraw());
    Serial.println("================");
  }
}

void loop() {
  handleButtons();
  processSerialData();
  debugData();
  
  // Full redraw quando muda de tela ou sub-modo: envia os 1024 bytes inteiros
  if (needsFullRedraw()) {
    u8g2.clearBuffer();
    drawHeader();
    drawContent();
    u8g2.sendBuffer();
    updatePrevState();
    return;
  }

  // SETTINGS: atualiza só o header quando o estado do [EDIT] piscante muda
  if (currentScreen == SETTINGS) {
    bool blinkOn = editMode && (millis() / 500) % 2 == 0;
    if (blinkOn != prevBlinkOn) {
      prevBlinkOn = blinkOn;
      u8g2.setDrawColor(0);
      u8g2.drawBox(0, 0, 128, CONTENT_Y); // apaga área do header no buffer
      u8g2.setDrawColor(1);
      drawHeader();
      u8g2.updateDisplayArea(0, HEADER_TY, TILE_COLS, HEADER_TH); // ~6ms
    }
  }

  // Partial update: apaga e redesenha só a área de conteúdo, envia 768 bytes (~18ms vs ~25ms)
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, CONTENT_Y, 128, CONTENT_H);
  u8g2.setDrawColor(1);
  drawContent();
  u8g2.updateDisplayArea(0, CONTENT_TY, TILE_COLS, CONTENT_TH);
}
