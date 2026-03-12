// =======
// TechNet
// =======

// 1. C++ Headers
#include <map>
#include <vector>
#include <algorithm>

// 2. UNDEFINE min/max before any Arduino or library headers to prevent macro conflict
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// 3. Include Arduino and Wio Terminal libraries
#include <Arduino.h>
#include <rpcWiFi.h>
#include <rpcBLEDevice.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include <SparkFunBQ27441.h>
#include <Seeed_FS.h>
#include "SD/Seeed_SD.h"
#include "Keyboard.h"
#include <DNSServer.h>
#include <WebServer.h>
#include "boot_logo_technet.h"

// 4. Define min/max for Arduino compatibility AFTER all headers
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

// ======= Wio Terminal controls =======
#define BTN_A WIO_KEY_A // R
#define BTN_B WIO_KEY_B // M
#define BTN_C WIO_KEY_C // L

#define JOY_UP    WIO_5S_UP
#define JOY_DOWN  WIO_5S_DOWN
#define JOY_LEFT  WIO_5S_LEFT
#define JOY_RIGHT WIO_5S_RIGHT
#define JOY_OK    WIO_5S_PRESS

// ======= Display =======
TFT_eSPI tft;
static const int W = 320;
static const int H = 240;

// ======= Named UI layout constants =======
static const uint16_t COLOR_BATTBAR_BG  = 0x1082; // very subtle upper battery strip
static const uint16_t COLOR_HEADER_BG   = 0x2104; // dark blue-grey header background
static const uint16_t COLOR_FOOTER_BG   = 0x1082; // darker blue-grey footer background
static const uint16_t COLOR_SELECTED_BG = 0x4208; // highlight row background (dark highlight)
static const uint16_t COLOR_TOGGLE_OFF  = 0x39E7; // toggle switch off-state colour

static const int UI_BATTBAR_H   = 12;  // tiny battery strip above main header
static const int UI_MAIN_HDR_Y  = 12;  // main header begins below battery strip
static const int UI_MAIN_HDR_H  = 45;  // old header height
static const int UI_HEADER_H    = 57;  // total top area
static const int UI_FOOTER_Y    = 215; // Y-position of the bottom footer bar
static const int UI_FOOTER_H    = 25;  // height of the footer bar
static const int UI_CONTENT_Y   = 57;  // top of the scrollable content area
static const int UI_CONTENT_H   = 158; // height of the scrollable content area

// ======= Battery / Fuel Gauge =======
static const unsigned int TN_BATT_CAPACITY_MAH = 650; // Seeed battery chassis wiki value
static const uint32_t TN_BATT_POLL_MS = 1500;

static bool     tn_battGaugeReady = false;
static bool     tn_battDetected   = false;
static bool     tn_battCharging   = false;
static uint8_t  tn_battPercent    = 0;
static uint16_t tn_battVoltageMv  = 0;
static int      tn_battCurrentMa  = 0;
static uint32_t tn_battLastPollMs = 0;

// ======= App framework =======
enum AppId { APP_MENU, APP_CHANNEL_MONITOR, APP_SSID_MONITOR, APP_SSID_SCAN, APP_BT_MONITOR, APP_BT_SCAN, APP_BADUSB, APP_WIOCHAT, APP_WIODROP, APP_SDTOOL };
AppId app = APP_MENU;

// ======= Serial debug/logging =======
#define TN_SERIAL_BAUD 115200
#define TN_SERIAL_WAIT_MS 1500

static bool tn_serialReady = false;

static const char* tn_appName(AppId id) {
  switch (id) {
    case APP_MENU:            return "MENU";
    case APP_CHANNEL_MONITOR: return "CHANNEL_MONITOR";
    case APP_SSID_MONITOR:    return "SSID_MONITOR";
    case APP_SSID_SCAN:       return "SSID_SCANNER";
    case APP_BT_MONITOR:      return "BLE_MAC_MONITOR";
    case APP_BT_SCAN:         return "BLE_MAC_SCANNER";
    case APP_BADUSB:          return "WIO_BADUSB";
    case APP_WIOCHAT:         return "WIO_CHAT";
    case APP_WIODROP:         return "WIO_DROP";
    case APP_SDTOOL:          return "SD_TOOL";
    default:                  return "UNKNOWN";
  }
}

static void tn_log(const String& tag, const String& msg) {
  if (!tn_serialReady) return;
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] [");
  Serial.print(tag);
  Serial.print("] ");
  Serial.println(msg);
}

static void tn_logf(const String& tag, const String& k, const String& v) {
  if (!tn_serialReady) return;
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] [");
  Serial.print(tag);
  Serial.print("] ");
  Serial.print(k);
  Serial.print(": ");
  Serial.println(v);
}

static void tn_logAppEnter(AppId id) {
  tn_log("APP", String("Enter ") + tn_appName(id));
}

static void tn_logAppExit(AppId id) {
  tn_log("APP", String("Exit ") + tn_appName(id));
}

static void tn_logKeyPress(const char* name) {
  tn_log("INPUT", String("Pressed ") + name);
}

// ======= Input bitmasks =======
static const uint16_t MASK_BTN_A     = 0x0001;
static const uint16_t MASK_BTN_B     = 0x0002;
static const uint16_t MASK_BTN_C     = 0x0004;
static const uint16_t MASK_JOY_UP    = 0x0010;
static const uint16_t MASK_JOY_DOWN  = 0x0020;
static const uint16_t MASK_JOY_LEFT  = 0x0040;
static const uint16_t MASK_JOY_RIGHT = 0x0080;
static const uint16_t MASK_JOY_OK    = 0x0100;

// ======= Battery Helpers =======
static uint16_t tn_battColorForPercent(int pct) {
  if (pct <= 20) return TFT_RED;
  if (pct <= 60) return TFT_YELLOW;
  return TFT_GREEN;
}

static void tn_battInit() {
  Wire.begin();

  if (lipo.begin()) {
    lipo.setCapacity(TN_BATT_CAPACITY_MAH);
    tn_battGaugeReady = true;
    tn_battDetected   = true;
    tn_log("BATT", "BQ27441 initialized");
  } else {
    tn_battGaugeReady = false;
    tn_battDetected   = false;
    tn_log("BATT", "BQ27441 not detected");
  }
}

static void tn_battPoll(bool force = false) {
  uint32_t now = millis();
  if (!force && (now - tn_battLastPollMs) < TN_BATT_POLL_MS) return;
  tn_battLastPollMs = now;

  if (!tn_battGaugeReady) {
    tn_battDetected = false;
    return;
  }

  unsigned int soc = lipo.soc();
  unsigned int voltage = lipo.voltage();
  int current = lipo.current(AVG);

  if (soc > 100 || voltage < 2500 || voltage > 5000) {
    tn_battDetected = false;
    return;
  }

  tn_battDetected  = true;
  tn_battPercent   = (uint8_t)soc;
  tn_battVoltageMv = (uint16_t)voltage;
  tn_battCurrentMa = current;
  tn_battCharging  = (current > 0);
}

static String tn_battStatusText() {
  if (!tn_battDetected) return "BAT%: N/A";
  String s = "BAT%: " + String((int)tn_battPercent);
  if (tn_battCharging) s += " charging";
  return s;
}

static void tn_drawBatteryStrip() {
  tn_battPoll(false);

  tft.fillRect(0, 0, W, UI_BATTBAR_H, COLOR_BATTBAR_BG);
  tft.setTextSize(1);

  String label = tn_battStatusText();
  int textWidth = label.length() * 6;

  if (!tn_battDetected) {
    tft.setTextColor(TFT_LIGHTGREY, COLOR_BATTBAR_BG);
  } else {
    tft.setTextColor(tn_battColorForPercent(tn_battPercent), COLOR_BATTBAR_BG);
  }

  tft.setCursor(W - 6 - textWidth, 2);
  tft.print(label);
}

// ======= UI Helpers =======
static void drawSkinHeader(String title, String status = "", uint16_t statusColor = TFT_CYAN) {
  tn_drawBatteryStrip();

  tft.fillRect(0, UI_MAIN_HDR_Y, W, UI_MAIN_HDR_H, COLOR_HEADER_BG);
  tft.setTextColor(TFT_WHITE, COLOR_HEADER_BG);
  tft.setTextSize(2);

  // Default: LEFT aligned
  tft.setCursor(15, UI_MAIN_HDR_Y + 15);
  tft.print(title);

  if (status != "") {
    tft.fillRoundRect(200, UI_MAIN_HDR_Y + 10, 110, 25, 4, statusColor);
    tft.setTextColor(TFT_BLACK, statusColor);
    tft.setTextSize(1);
    int textWidth = status.length() * 6;
    tft.setCursor(200 + (110 - textWidth) / 2, UI_MAIN_HDR_Y + 18);
    tft.print(status);
  }
}

static void drawSkinFooter(String text) {
  tft.fillRect(0, UI_FOOTER_Y, W, UI_FOOTER_H, COLOR_FOOTER_BG);
  tft.setTextColor(TFT_LIGHTGREY, COLOR_FOOTER_BG);
  tft.setTextSize(1);

  int textWidth = (int)text.length() * 6;   // textSize(1) ≈ 6px/char
  int x = (W - textWidth) / 2;
  if (x < 0) x = 0;

  tft.setCursor(x, 223);
  tft.print(text);
}

// ======= Boot logo =======
static inline uint16_t tn_byteswap16(uint16_t v) {
  return (uint16_t)((v >> 8) | (v << 8));
}

static void tn_drawSubtitle() {
  String sub = "by GH0ST3CH";
  int textSize = 1;
  int charW = 6 * textSize;
  int textW = sub.length() * charW;
  int x = (W - textW) / 2;
  int y = 140;

  tft.setTextSize(textSize);

  // subtle glow
  tft.setTextColor(TFT_BLUE);
  tft.setCursor(x - 1, y); tft.print(sub);
  tft.setCursor(x + 1, y); tft.print(sub);
  tft.setCursor(x, y - 1); tft.print(sub);
  tft.setCursor(x, y + 1); tft.print(sub);

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(x, y);
  tft.print(sub);
}

static void drawBootLogo() {
  static uint16_t line[TECHNET_BOOT_W];

  for (int y = 0; y < TECHNET_BOOT_H; y++) {
    for (int x = 0; x < TECHNET_BOOT_W; x++) {
      uint16_t v = pgm_read_word(&bootImg[y * TECHNET_BOOT_W + x]);
      line[x] = tn_byteswap16(v);  // Mode 1 confirmed correct
    }
    tft.pushImage(0, y, TECHNET_BOOT_W, 1, line);
  }

  tn_drawSubtitle();
  delay(1200);               // optional boot hold
  tft.fillScreen(TFT_BLACK); // optional clear after
}

struct Keys {
  uint16_t now = 0, last = 0;
  void update() { last = now; now = 0; }
  bool pressed(uint16_t m) const { return (now & m) && !(last & m); }
};
static Keys keys;

static const uint32_t BT_KEY_IDLE_MS    = 1500;
static uint32_t       bt_lastKeyMs      = 0;
static TaskHandle_t   bt_scanTaskHandle = NULL;
static volatile bool  bt_scanBusy       = false;

static inline void readInputs() {
  keys.update();
  if (digitalRead(BTN_A)     == LOW) keys.now |= MASK_BTN_A;
  if (digitalRead(BTN_B)     == LOW) keys.now |= MASK_BTN_B;
  if (digitalRead(BTN_C)     == LOW) keys.now |= MASK_BTN_C;
  if (digitalRead(JOY_UP)    == LOW) keys.now |= MASK_JOY_UP;
  if (digitalRead(JOY_DOWN)  == LOW) keys.now |= MASK_JOY_DOWN;
  if (digitalRead(JOY_LEFT)  == LOW) keys.now |= MASK_JOY_LEFT;
  if (digitalRead(JOY_RIGHT) == LOW) keys.now |= MASK_JOY_RIGHT;
  if (digitalRead(JOY_OK)    == LOW) keys.now |= MASK_JOY_OK;
  if (keys.now) bt_lastKeyMs = millis();

  if (keys.pressed(MASK_BTN_A))     tn_logKeyPress("BTN_A");
  if (keys.pressed(MASK_BTN_B))     tn_logKeyPress("BTN_B");
  if (keys.pressed(MASK_BTN_C))     tn_logKeyPress("BTN_C");
  if (keys.pressed(MASK_JOY_UP))    tn_logKeyPress("JOY_UP");
  if (keys.pressed(MASK_JOY_DOWN))  tn_logKeyPress("JOY_DOWN");
  if (keys.pressed(MASK_JOY_LEFT))  tn_logKeyPress("JOY_LEFT");
  if (keys.pressed(MASK_JOY_RIGHT)) tn_logKeyPress("JOY_RIGHT");
  if (keys.pressed(MASK_JOY_OK))    tn_logKeyPress("JOY_OK");
}

static inline void wifiInitOnce() {
  static bool inited = false;
  if (inited) return;
  tn_log("WIFI", "Initializing STA mode");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  inited = true;
  tn_log("WIFI", "STA ready and disconnected");
}

// ===================== MENU =====================
static const uint8_t CAT_WIFI = 0;
static const uint8_t CAT_BT   = 1;
static const uint8_t CAT_USB  = 2;
static const uint8_t CAT_OTHER = 3;

static uint8_t menuCat = CAT_WIFI;
static int menuIndex = 0;
static int menuScroll = 0;

static const int MENU_ROW0_Y   = 71;
static const int MENU_ROW_STEP = 30;
static const int MENU_VISIBLE  = 5;

static void menu_getItems(uint8_t cat, const char*** outItems, int* outCount) {
  static const char* wifiItems[]  = {"Channel Monitor", "SSID Monitor", "SSID Scanner", "Wio Chat", "Wio Drop"};
  static const char* btItems[]    = {"BLE MAC Monitor", "BLE MAC Scanner"};
  static const char* usbItems[]   = {"Wio BadUSB"};
  static const char* otherItems[] = {"SD Tool"};

  if (cat == CAT_WIFI) { *outItems = wifiItems; *outCount = 5; }
  else if (cat == CAT_BT) { *outItems = btItems; *outCount = 2; }
  else if (cat == CAT_USB) { *outItems = usbItems; *outCount = 1; }
  else { *outItems = otherItems; *outCount = 1; }
}

static void menu_drawRowAt(const char** items, int itemIndex, int selectedIndex, int screenRow) {
  int y = MENU_ROW0_Y + screenRow * MENU_ROW_STEP;

  tft.fillRect(0, y - 6, W, 28, TFT_BLACK);

  int rowX = 8;
  int rowW = W - 16;

  if (itemIndex == selectedIndex) {
    tft.fillRoundRect(rowX, y - 5, rowW, 26, 6, COLOR_SELECTED_BG);
    tft.setTextColor(TFT_YELLOW);
  } else {
    tft.setTextColor(TFT_WHITE);
  }

  tft.setTextSize(2);

  String label = items[itemIndex];
  int textWidth = label.length() * 12;   // textSize(2) ≈ 12 px per char
  int x = rowX + (rowW - textWidth) / 2;

  if (x < rowX) x = rowX;

  tft.setCursor(x, y);
  tft.print(label);
}

static void drawMenuItems(const char** items, int count, int selectedIndex) {
  int end = menuScroll + MENU_VISIBLE;
  if (end > count) end = count;

  int screenRow = 0;
  for (int i = menuScroll; i < end; i++) {
    menu_drawRowAt(items, i, selectedIndex, screenRow);
    screenRow++;
  }
}

static void menu_drawHeaderOnce() {
  tn_drawBatteryStrip();

  tft.fillRect(0, UI_MAIN_HDR_Y, W, UI_MAIN_HDR_H, COLOR_HEADER_BG);

  tft.setTextSize(3);
  tft.setTextColor(TFT_LIGHTGREY, COLOR_HEADER_BG);
  tft.setCursor(7, UI_MAIN_HDR_Y + 13);
  tft.print("<");
  tft.setCursor(297, UI_MAIN_HDR_Y + 13);
  tft.print(">");
}

static void menu_drawTitleOnly() {
  String title = (menuCat == CAT_WIFI)  ? "WiFi Apps"  :
                 (menuCat == CAT_BT)    ? "BLE Apps"   :
                 (menuCat == CAT_USB)   ? "USB Apps"   :
                                          "Other Apps";

  tft.fillRect(40, UI_MAIN_HDR_Y, W - 80, UI_MAIN_HDR_H, COLOR_HEADER_BG);

  tft.setTextColor(TFT_CYAN, COLOR_HEADER_BG);
  tft.setTextSize(3);
  int titleWidth = title.length() * 17;
  int x = (W - titleWidth) / 2;
  if (x < 40) x = 40;
  if (x > (W - 40 - titleWidth)) x = (W - 40 - titleWidth);
  tft.setCursor(x, UI_MAIN_HDR_Y + 13);
  tft.print(title);
}

static void menu_drawFull() {
  tft.fillScreen(TFT_BLACK);
  menu_drawHeaderOnce();
  menu_drawTitleOnly();

  const char** items = nullptr;
  int count = 0;
  menu_getItems(menuCat, &items, &count);
  drawMenuItems(items, count, menuIndex);

  drawSkinFooter("LB/RB/L/R: Category   U/D: Select   OK: Launch");
}

static void menu_redrawItemsOnly() {
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  const char** items = nullptr;
  int count = 0;
  menu_getItems(menuCat, &items, &count);
  drawMenuItems(items, count, menuIndex);
}

static void menu_draw() {
  menu_drawFull();
}

static void menu_loop() {
  // Category left
  if (keys.pressed(MASK_JOY_LEFT) || keys.pressed(MASK_BTN_C)) {
    menuCat = (menuCat == CAT_WIFI)  ? CAT_OTHER :
              (menuCat == CAT_OTHER) ? CAT_USB   :
              (menuCat == CAT_USB)   ? CAT_BT    :
                                       CAT_WIFI;
    menuIndex = 0;
    menuScroll = 0;

    menu_drawTitleOnly();
    menu_redrawItemsOnly();
    return;
  }

  // Category right
  if (keys.pressed(MASK_JOY_RIGHT) || keys.pressed(MASK_BTN_A)) {
    menuCat = (menuCat == CAT_WIFI) ? CAT_BT    :
              (menuCat == CAT_BT)   ? CAT_USB   :
              (menuCat == CAT_USB)  ? CAT_OTHER :
                                      CAT_WIFI;
    menuIndex = 0;
    menuScroll = 0;

    menu_drawTitleOnly();
    menu_redrawItemsOnly();
    return;
  }

  // Current item list
  const char** items = nullptr;
  int count = 0;
  menu_getItems(menuCat, &items, &count);
  if (count <= 0) return;

  if (keys.pressed(MASK_JOY_UP)) {
    int oldIndex = menuIndex;
    int oldScroll = menuScroll;

    menuIndex = (menuIndex - 1 + count) % count;

    if (menuIndex < menuScroll) menuScroll = menuIndex;
    if (menuIndex >= menuScroll + MENU_VISIBLE) menuScroll = menuIndex - MENU_VISIBLE + 1;

    // handle wrap (0 -> last)
    if (oldIndex == 0 && menuIndex == count - 1) {
      menuScroll = (count > MENU_VISIBLE) ? (count - MENU_VISIBLE) : 0;
    }

    if (menuScroll != oldScroll) {
      menu_redrawItemsOnly();
    } else {
      int oldRow = oldIndex - menuScroll;
      int newRow = menuIndex - menuScroll;
      if (oldRow >= 0 && oldRow < MENU_VISIBLE) menu_drawRowAt(items, oldIndex, menuIndex, oldRow);
      if (newRow >= 0 && newRow < MENU_VISIBLE) menu_drawRowAt(items, menuIndex, menuIndex, newRow);
    }
  }

  if (keys.pressed(MASK_JOY_DOWN)) {
    int oldIndex = menuIndex;
    int oldScroll = menuScroll;

    menuIndex = (menuIndex + 1) % count;

    if (menuIndex < menuScroll) menuScroll = menuIndex;
    if (menuIndex >= menuScroll + MENU_VISIBLE) menuScroll = menuIndex - MENU_VISIBLE + 1;

    // handle wrap (last -> 0)
    if (oldIndex == count - 1 && menuIndex == 0) {
      menuScroll = 0;
    }

    if (menuScroll != oldScroll) {
      menu_redrawItemsOnly();
    } else {
      int oldRow = oldIndex - menuScroll;
      int newRow = menuIndex - menuScroll;
      if (oldRow >= 0 && oldRow < MENU_VISIBLE) menu_drawRowAt(items, oldIndex, menuIndex, oldRow);
      if (newRow >= 0 && newRow < MENU_VISIBLE) menu_drawRowAt(items, menuIndex, menuIndex, newRow);
    }
  }

  // Launch
  if (keys.pressed(MASK_JOY_OK)) {
    if (menuCat == CAT_WIFI) {
      app = (menuIndex == 0) ? APP_CHANNEL_MONITOR
          : (menuIndex == 1) ? APP_SSID_MONITOR
          : (menuIndex == 2) ? APP_SSID_SCAN
          : (menuIndex == 3) ? APP_WIOCHAT
                             : APP_WIODROP;
    } else if (menuCat == CAT_BT) {
      app = (menuIndex == 0) ? APP_BT_MONITOR
                             : APP_BT_SCAN;
    } else if (menuCat == CAT_USB) {
      app = APP_BADUSB;
    } else {
      app = APP_SDTOOL;
    }

    tn_log("MENU", String("Launch request cat=") + String(menuCat) + " index=" + String(menuIndex));
    tn_logAppEnter(app);
    tft.fillScreen(TFT_BLACK);
  }
}

// ======= Channel Monitor App =======
enum CM_Band { CM_BAND_24, CM_BAND_5 };

static CM_Band cm_band = CM_BAND_24;
static const int CM_MAX_CHANNEL = 196;
static uint8_t cm_apCount[CM_MAX_CHANNEL];

static const uint8_t cm_ch24[] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
static int cm_chIdx24 = 5;

static const uint8_t cm_ch5[]  = {36,40,44,46,48,149,153,157,161,165};
static int cm_chIdx5 = 0;

static bool cm_isScanning = false;

static void cm_draw() {
  drawSkinHeader("Channel Monitor", (cm_band == CM_BAND_24 ? "2.4GHz" : "5GHz"));

  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  const uint8_t* chans = (cm_band == CM_BAND_24) ? cm_ch24 : cm_ch5;

  int nCh = (cm_band == CM_BAND_24)
              ? (int)(sizeof(cm_ch24) / sizeof(cm_ch24[0]))
              : (int)(sizeof(cm_ch5)  / sizeof(cm_ch5[0]));

  int gap    = 2;
  int cellW  = W / nCh;
  int drawW  = cellW - gap;

  int totalWidth = (drawW * nCh) + (gap * (nCh - 1));
  int startX = (W - totalWidth) / 2;

  for (int i = 0; i < nCh; i++) {
    int x = startX + i * (drawW + gap);
    int h = map(cm_apCount[chans[i]], 0, 20, 0, 108);

    tft.fillRect(x, 184 - h, drawW, h, TFT_WHITE);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);

    String chStr  = String(chans[i]);
    int    textW  = chStr.length() * 6;
    int    centerX = x + (drawW - textW) / 2;

    tft.setCursor(centerX, 195);
    tft.print(chStr);

    if (cm_apCount[chans[i]] > 0) {
      tft.setTextColor(TFT_CYAN);

      String apStr   = String(cm_apCount[chans[i]]);
      int    apTextW = apStr.length() * 6;
      int    apCenterX = x + (drawW - apTextW) / 2;

      tft.setCursor(apCenterX, 190 - h - 15);
      tft.print(apStr);
    }
  }

  drawSkinFooter("LB/OK:Exit   MB:Change Band");
}

static void cm_enter() {
  wifiInitOnce();
  memset(cm_apCount, 0, sizeof(cm_apCount));
  cm_draw();
  cm_isScanning = false;
  tn_logAppEnter(APP_CHANNEL_MONITOR);
  tn_log("WIFI", "Channel Monitor ready");
}

static void cm_loop() {
  if (keys.pressed(MASK_BTN_B)) {
    cm_band = (cm_band == CM_BAND_24 ? CM_BAND_5 : CM_BAND_24);
    tn_log("WIFI", String("Channel Monitor band change to ") + (cm_band == CM_BAND_24 ? "2.4GHz" : "5GHz"));
    cm_draw();
  }

  if (keys.pressed(MASK_BTN_C) || keys.pressed(MASK_JOY_OK)) {
    tn_logAppExit(APP_CHANNEL_MONITOR);
    app = APP_MENU;
    menu_draw();
    tn_logAppEnter(APP_MENU);
  }

  if (!cm_isScanning) {
    tn_log("WIFI", String("Starting async channel scan band=") + (cm_band == CM_BAND_24 ? "2.4GHz" : "5GHz"));
    WiFi.scanNetworks(true);
    cm_isScanning = true;
  } else {
    int n = WiFi.scanComplete();

    if (n >= 0) {
      tn_log("WIFI", String("Channel scan complete, AP records=") + String(n));
      memset(cm_apCount, 0, sizeof(cm_apCount));

      for (int i = 0; i < n; i++) {
        int ch = WiFi.channel(i);
        if (ch < CM_MAX_CHANNEL) cm_apCount[ch]++;
      }

      cm_draw();
      WiFi.scanDelete();
      cm_isScanning = false;
    }
  }
}

// ===================== SSID MONITOR =====================
struct MonAPRow {
  String ssid;
  String bssid;
  int    ch;
  int    rssi;
  bool   is5G;
};

static std::vector<MonAPRow> mon_rows;
static int  mon_sel       = 0;
static int  mon_scroll    = 0;
static bool mon_scanning  = false;

static bool   mon_detailOn = false;
static String mon_selBSSID = "";
static MonAPRow mon_detail;
static int      mon_lastDetailRSSI = 9999;

static const int MON_VISIBLE = 8;

static const int MON_COL_SSID_X = 15;
static const int MON_COL_CH_X   = 165;
static const int MON_COL_BAND_X = 215;
static const int MON_COL_RSSI_X = 265;

static const int MON_SSID_MAX_CHARS = 22;

static const int MON_DETAIL_MAX_CHARS = 18;

static const int MON_HDR_Y    = 64;
static const int MON_ROW0_Y   = 86;
static const int MON_ROW_STEP = 18;
static const int MON_HI_PAD_Y = 4;

static const int MON_DET_X_LABEL = 10;
static const int MON_DET_Y_SSID  = 72;
static const int MON_DET_Y_MAC   = 102;
static const int MON_DET_Y_CH    = 132;
static const int MON_DET_Y_BAND  = 162;
static const int MON_DET_Y_RSSI  = 192;

static const int MON_DET_RSSI_VAL_X   = 10 + (6 * 12);
static const int MON_DET_RSSI_CLEAR_W = 320 - 20 - MON_DET_RSSI_VAL_X;

static const uint32_t MON_KEY_IDLE_MS           = 1500;
static uint32_t       mon_lastScanMs            = 0;

static String mon_ssidFit(String s) {
  if (s == "") s = "<HIDDEN>";
  if ((int)s.length() <= MON_SSID_MAX_CHARS) return s;
  return s.substring(0, MON_SSID_MAX_CHARS - 3) + "...";
}

static String mon_detailFit(String s) {
  if (s == "") s = "<HIDDEN>";
  if ((int)s.length() <= MON_DETAIL_MAX_CHARS) return s;
  return s.substring(0, MON_DETAIL_MAX_CHARS - 3) + "...";
}

static void mon_drawScrollBar(int total, int start) {
  const int x = 308;
  const int y = MON_ROW0_Y - 14;
  const int h = (MON_VISIBLE * MON_ROW_STEP);
  const int w = 4;

  tft.fillRect(x, y, w, h, 0x2104);

  if (total <= MON_VISIBLE || total <= 0) {
    tft.fillRect(x, y, w, h, TFT_CYAN);
    return;
  }

  int thumbH = max(10, (h * MON_VISIBLE) / total);
  int maxStart = total - MON_VISIBLE;
  int thumbY = y + ((h - thumbH) * start) / maxStart;

  tft.fillRect(x, thumbY, w, thumbH, TFT_CYAN);
}

static int mon_findIndexByBSSID(const String& bssid) {
  for (int i = 0; i < (int)mon_rows.size(); i++) {
    if (mon_rows[i].bssid == bssid) return i;
  }
  return -1;
}

static void mon_drawList() {
  drawSkinHeader("SSID Monitor", "APs: " + String((int)mon_rows.size()));
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(MON_COL_SSID_X, MON_HDR_Y); tft.print("SSID");
  tft.setCursor(MON_COL_CH_X,   MON_HDR_Y); tft.print("CH");
  tft.setCursor(MON_COL_BAND_X, MON_HDR_Y); tft.print("BAND");
  tft.setCursor(MON_COL_RSSI_X, MON_HDR_Y); tft.print("RSSI");

  int total = (int)mon_rows.size();
  int start = mon_scroll;
  int end   = min(start + MON_VISIBLE, total);

  for (int row = 0; row < (end - start); row++) {
    int i = start + row;
    int y = MON_ROW0_Y + row * MON_ROW_STEP;

    if (i == mon_sel) {
      tft.fillRoundRect(5, y - MON_HI_PAD_Y, W - 10, 16, 4, COLOR_SELECTED_BG);
      tft.setTextColor(TFT_YELLOW);
    } else {
      tft.setTextColor(TFT_WHITE);
    }

    tft.setCursor(MON_COL_SSID_X, y);
    tft.print(mon_ssidFit(mon_rows[i].ssid));

    tft.setCursor(MON_COL_CH_X, y);
    tft.print(mon_rows[i].ch);

    tft.setCursor(MON_COL_BAND_X, y);
    tft.print(mon_rows[i].is5G ? "5GHz" : "2.4GHz");

    tft.setCursor(MON_COL_RSSI_X, y);
    tft.print(mon_rows[i].rssi);
  }

  mon_drawScrollBar(total, mon_scroll);
  drawSkinFooter("LB:Exit   U/D:Select   OK:Details");
}

static void mon_drawDetailFull() {
  String badge = (mon_detail.bssid.length() ? (String(mon_detail.rssi) + " dBm") : String("N/A"));
  drawSkinHeader("AP Details", badge, TFT_CYAN);
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);

  String ss = mon_detailFit(mon_detail.ssid);

  tft.setCursor(MON_DET_X_LABEL, MON_DET_Y_SSID); tft.print("SSID: "); tft.print(ss);
  tft.setCursor(MON_DET_X_LABEL, MON_DET_Y_MAC);  tft.print("MAC: ");  tft.print(mon_detail.bssid);
  tft.setCursor(MON_DET_X_LABEL, MON_DET_Y_CH);   tft.print("CH: ");   tft.print(mon_detail.ch);
  tft.setCursor(MON_DET_X_LABEL, MON_DET_Y_BAND); tft.print("Band: "); tft.print(mon_detail.is5G ? "5GHz" : "2.4GHz");

  tft.setCursor(MON_DET_X_LABEL, MON_DET_Y_RSSI); tft.print("RSSI: ");
  tft.print(mon_detail.rssi);
  tft.print(" dBm");

  drawSkinFooter("Press any button to return");
  mon_lastDetailRSSI = mon_detail.rssi;
}

static void mon_updateDetailRSSIOnly() {
  if (mon_detail.rssi == mon_lastDetailRSSI) return;

  tft.fillRoundRect(200, UI_MAIN_HDR_Y + 10, 110, 25, 4, TFT_CYAN);
  tft.setTextColor(TFT_BLACK, TFT_CYAN);
  tft.setTextSize(1);
  String status = String(mon_detail.rssi) + " dBm";
  int textWidth = status.length() * 6;
  tft.setCursor(200 + (110 - textWidth) / 2, UI_MAIN_HDR_Y + 18);
  tft.print(status);

  tft.fillRect(MON_DET_RSSI_VAL_X, MON_DET_Y_RSSI - 2, MON_DET_RSSI_CLEAR_W, 18, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(MON_DET_RSSI_VAL_X, MON_DET_Y_RSSI);
  tft.print(mon_detail.rssi);
  tft.print(" dBm");

  mon_lastDetailRSSI = mon_detail.rssi;
}

static void mon_kickScanIfNeeded() {
  if (!mon_scanning) {
    tn_log("WIFI", "SSID Monitor async scan start");
    WiFi.scanNetworks(true);
    mon_scanning = true;
  }
}

static void mon_kickDetailScanIfNeeded() {
  if (!mon_scanning) {
    tn_log("WIFI", "SSID Monitor detail refresh scan start");
    WiFi.scanNetworks(true, false, false, 120);
    mon_scanning = true;
  }
}

static void mon_onScanComplete() {
  int n = WiFi.scanComplete();
  if (n < 0) return;

  tn_log("WIFI", String("SSID Monitor scan complete, APs=") + String(n));

  mon_rows.clear();
  mon_rows.reserve((size_t)n);

  for (int i = 0; i < n; i++) {
    MonAPRow ap;
    ap.ssid  = WiFi.SSID(i);
    ap.bssid = WiFi.BSSIDstr(i);
    ap.ch    = WiFi.channel(i);
    ap.rssi  = WiFi.RSSI(i);
    ap.is5G  = (ap.ch > 14);
    mon_rows.push_back(ap);
  }

  std::sort(mon_rows.begin(), mon_rows.end(),
    [](const MonAPRow& a, const MonAPRow& b) { return a.rssi > b.rssi; });

  int total = (int)mon_rows.size();
  if (mon_sel >= total) mon_sel = total ? total - 1 : 0;
  if (mon_sel < 0) mon_sel = 0;

  if (mon_sel < mon_scroll) mon_scroll = mon_sel;
  if (mon_sel >= mon_scroll + MON_VISIBLE) mon_scroll = mon_sel - (MON_VISIBLE - 1);
  if (mon_scroll < 0) mon_scroll = 0;

  if (mon_detailOn && mon_selBSSID.length()) {
    int idx = mon_findIndexByBSSID(mon_selBSSID);
    if (idx >= 0) {
      mon_detail = mon_rows[idx];
      mon_updateDetailRSSIOnly();
    }
  } else {
    mon_drawList();
  }

  WiFi.scanDelete();
  mon_scanning = false;
}

static void mon_enter() {
  wifiInitOnce();
  mon_rows.clear();
  mon_sel    = 0;
  mon_scroll = 0;
  mon_scanning = false;

  mon_detailOn       = false;
  mon_selBSSID       = "";
  mon_lastDetailRSSI = 9999;

  mon_lastScanMs       = 0;

  mon_drawList();
  mon_kickScanIfNeeded();
  tn_logAppEnter(APP_SSID_MONITOR);
  tn_log("WIFI", "SSID Monitor ready");
}

static void mon_loop() {
  if (mon_detailOn) {
    if (keys.now != 0) {
      mon_detailOn = false;
      mon_drawList();
    }
  } else {
    int total = (int)mon_rows.size();

    if (keys.pressed(MASK_JOY_UP)) {
      if (total > 0 && mon_sel > 0) mon_sel--;
      if (mon_sel < mon_scroll) mon_scroll = mon_sel;
      mon_drawList();
    }

    if (keys.pressed(MASK_JOY_DOWN)) {
      if (total > 0 && mon_sel < total - 1) mon_sel++;
      if (mon_sel >= mon_scroll + MON_VISIBLE) mon_scroll = mon_sel - (MON_VISIBLE - 1);
      mon_drawList();
    }

    if (keys.pressed(MASK_JOY_OK) && total > 0) {
      tn_log("WIFI", String("SSID detail open: ") + mon_rows[mon_sel].bssid);
      mon_selBSSID = mon_rows[mon_sel].bssid;
      mon_detail   = mon_rows[mon_sel];
      mon_detailOn = true;
      mon_drawDetailFull();
    }

    if (keys.pressed(MASK_BTN_C)) {
      tn_logAppExit(APP_SSID_MONITOR);
      app = APP_MENU;
      menu_draw();
      tn_logAppEnter(APP_MENU);
      return;
    }
  }

  uint32_t now = millis();

  bool allowScan = !(bt_lastKeyMs != 0 && (now - bt_lastKeyMs) < MON_KEY_IDLE_MS);

  if (mon_detailOn) {
    if (!mon_scanning && (now - mon_lastScanMs) >= 500) {
      mon_lastScanMs = now;
      mon_kickDetailScanIfNeeded();
    }
  } else {
    if (allowScan) mon_kickScanIfNeeded();
  }

  mon_onScanComplete();
}

// ===================== SSID SCANNER =====================
struct APInfo { String ssid; String bssid; int ch; int rssi; bool is5G; };

static std::vector<APInfo> ss_networks;
static int  ss_sel        = 0;
static int  ss_scroll     = 0;
static bool ss_showDetail = false;

static const int SS_VISIBLE = 8;

static const int SS_COL_SSID_X = 15;
static const int SS_COL_CH_X   = 165;
static const int SS_COL_BAND_X = 215;
static const int SS_COL_RSSI_X = 265;

static const int SS_SSID_MAX_CHARS = 22;

static const int SS_HDR_Y    = 64;
static const int SS_ROW0_Y   = 86;
static const int SS_ROW_STEP = 18;
static const int SS_HI_PAD_Y = 4;

static String ss_ssidFit(String s) {
  if (s == "") s = "<HIDDEN>";
  if ((int)s.length() <= SS_SSID_MAX_CHARS) return s;
  return s.substring(0, SS_SSID_MAX_CHARS - 3) + "...";
}

static void ss_drawScrollBar(int total, int start) {
  const int x = 308;
  const int y = SS_ROW0_Y - 14;
  const int h = (SS_VISIBLE * SS_ROW_STEP);
  const int w = 4;

  tft.fillRect(x, y, w, h, 0x2104);

  if (total <= SS_VISIBLE || total <= 0) {
    tft.fillRect(x, y, w, h, TFT_CYAN);
    return;
  }

  int thumbH = max(10, (h * SS_VISIBLE) / total);
  int maxStart = total - SS_VISIBLE;
  int thumbY = y + ((h - thumbH) * start) / maxStart;

  tft.fillRect(x, thumbY, w, thumbH, TFT_CYAN);
}

static void ss_draw() {
  if (ss_showDetail) {
    APInfo& ap = ss_networks[ss_sel];
    drawSkinHeader("AP Details");
    tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);

    String s = ap.ssid;
    if (s == "") s = "<HIDDEN>";
    if (s.length() > 24) s = s.substring(0, 24) + "...";

    tft.setCursor(10, 72);  tft.print("SSID: "); tft.print(s);
    tft.setCursor(10, 102); tft.print("MAC: ");  tft.print(ap.bssid);
    tft.setCursor(10, 132); tft.print("CH: ");   tft.print(ap.ch);
    tft.setCursor(10, 162); tft.print("Band: "); tft.print(ap.is5G ? "5GHz" : "2.4GHz");
    tft.setCursor(10, 192); tft.print("RSSI: "); tft.print(ap.rssi); tft.print(" dBm");

    drawSkinFooter("Press any button to return");
    return;
  }

  drawSkinHeader("SSID Scanner", "APs: " + String((int)ss_networks.size()));
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(SS_COL_SSID_X, SS_HDR_Y); tft.print("SSID");
  tft.setCursor(SS_COL_CH_X,   SS_HDR_Y); tft.print("CH");
  tft.setCursor(SS_COL_BAND_X, SS_HDR_Y); tft.print("BAND");
  tft.setCursor(SS_COL_RSSI_X, SS_HDR_Y); tft.print("RSSI");

  int total = (int)ss_networks.size();
  int start = ss_scroll;
  int end   = min(start + SS_VISIBLE, total);

  for (int row = 0; row < (end - start); row++) {
    int i = start + row;
    int y = SS_ROW0_Y + row * SS_ROW_STEP;

    if (i == ss_sel) {
      tft.fillRoundRect(5, y - SS_HI_PAD_Y, W - 10, 16, 4, COLOR_SELECTED_BG);
      tft.setTextColor(TFT_YELLOW);
    } else {
      tft.setTextColor(TFT_WHITE);
    }

    tft.setTextSize(1);

    tft.setCursor(SS_COL_SSID_X, y);
    tft.print(ss_ssidFit(ss_networks[i].ssid));

    tft.setCursor(SS_COL_CH_X, y);
    tft.print(ss_networks[i].ch);

    tft.setCursor(SS_COL_BAND_X, y);
    tft.print(ss_networks[i].is5G ? "5GHz" : "2.4GHz");

    tft.setCursor(SS_COL_RSSI_X, y);
    tft.print(ss_networks[i].rssi);
  }

  ss_drawScrollBar(total, ss_scroll);
  drawSkinFooter("LB:Exit   RB:Scan   U/D:Select   OK:Details");
}

static void ss_scanOnce() {
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(100, 119);
  tft.print("SCANNING...");
  drawSkinFooter("Please wait..");

  ss_networks.clear();

  tn_log("WIFI", "SSID Scanner blocking scan start");
  int n = WiFi.scanNetworks();
  tn_log("WIFI", String("SSID Scanner blocking scan complete, APs=") + String(n));

  for (int i = 0; i < n; i++) {
    APInfo ap;
    ap.ssid  = WiFi.SSID(i);
    ap.bssid = WiFi.BSSIDstr(i);
    ap.ch    = WiFi.channel(i);
    ap.rssi  = WiFi.RSSI(i);
    ap.is5G  = (ap.ch > 14);
    ss_networks.push_back(ap);
  }

  std::sort(ss_networks.begin(), ss_networks.end(),
    [](const APInfo& a, const APInfo& b) { return a.rssi > b.rssi; });

  ss_sel = 0;
  ss_scroll = 0;
  ss_showDetail = false;
  ss_draw();
}

static void ss_enter() {
  wifiInitOnce();

  ss_networks.clear();
  ss_sel = 0;
  ss_scroll = 0;
  ss_showDetail = false;

  drawSkinHeader("SSID Scanner");
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);
  drawSkinFooter("Please wait..");

  delay(1000);
  ss_scanOnce();
  tn_logAppEnter(APP_SSID_SCAN);
  tn_log("WIFI", "SSID Scanner ready");
}

static void ss_loop() {
  if (ss_showDetail) {
    if (keys.now != 0) { ss_showDetail = false; ss_draw(); }
    return;
  }

  int total = (int)ss_networks.size();

  if (keys.pressed(MASK_JOY_UP)) {
    if (total > 0 && ss_sel > 0) ss_sel--;
    if (ss_sel < ss_scroll) ss_scroll = ss_sel;
    ss_draw();
  }

  if (keys.pressed(MASK_JOY_DOWN)) {
    if (total > 0 && ss_sel < total - 1) ss_sel++;
    if (ss_sel >= ss_scroll + SS_VISIBLE) ss_scroll = ss_sel - (SS_VISIBLE - 1);
    ss_draw();
  }

  if (keys.pressed(MASK_JOY_OK) && total > 0) {
    ss_showDetail = true;
    ss_draw();
  }

  if (keys.pressed(MASK_BTN_C)) {
    tn_logAppExit(APP_SSID_SCAN);
    app = APP_MENU;
    menu_draw();
    tn_logAppEnter(APP_MENU);
  }

  if (keys.pressed(MASK_BTN_A)) {
    ss_scanOnce();
  }
}

// ======= BLE MONITOR =======

static String tn_fitText(String s, int maxChars) {
  if (maxChars <= 0) return "";
  if ((int)s.length() <= maxChars) return s;
  if (maxChars <= 3) return s.substring(0, maxChars);
  return s.substring(0, maxChars - 3) + "...";
}

static String tn_hex16(uint16_t v) {
  const char* hex = "0123456789ABCDEF";
  String s = "0x";
  s += hex[(v >> 12) & 0xF];
  s += hex[(v >> 8)  & 0xF];
  s += hex[(v >> 4)  & 0xF];
  s += hex[v & 0xF];
  return s;
}

struct BLERow {
  String mac;
  int    rssi;

  String name;
  String listLabel;

  bool     haveTX = false;
  int      tx     = 0;

  bool     haveAppearance = false;
  uint16_t appearance     = 0;

  bool     haveSvcUUID = false;

  bool     haveMfg = false;
};

static std::vector<BLERow> bt_rows;
static int  bt_sel      = 0;
static int  bt_scroll   = 0;
static bool bt_detailOn = false;
static String bt_selMAC = "";
static BLERow bt_detail;
static void bt_tryResolveNameForDetail();
static int bt_lastDetailRSSI = 9999;

static bool bt_inited = false;

static uint32_t bt_lastScanMs = 0;
static const uint32_t BT_SCAN_PERIOD_MS        = 1000;
static const uint32_t BT_DETAIL_SCAN_PERIOD_MS = 500;
static uint32_t       bt_lastDetailScanMs       = 0;

static const int BT_VISIBLE = 8;

static const int BT_COL_MAC_X  = 15;
static const int BT_COL_RSSI_X = 250;

static const int BT_HDR_Y    = 64;
static const int BT_ROW0_Y   = 86;
static const int BT_ROW_STEP = 18;
static const int BT_HI_PAD_Y = 4;

static const int BT_LIST_LABEL_MAX_CHARS = 20;

static const int BT_DET_X_LABEL = 10;
static const int BT_DET_Y_NAME  = 72;
static const int BT_DET_Y_MAC   = 102;
static const int BT_DET_Y_RSSI  = 132;

static const int BT_DET_RSSI_VAL_X   = BT_DET_X_LABEL + (6 * 12);
static const int BT_DET_RSSI_CLEAR_W = 320 - 10 - BT_DET_RSSI_VAL_X;

static void bt_drawScrollBar(int total, int start) {
  const int x = 308;
  const int y = BT_ROW0_Y - 14;
  const int h = (BT_VISIBLE * BT_ROW_STEP);
  const int w = 4;

  tft.fillRect(x, y, w, h, 0x2104);

  if (total <= BT_VISIBLE || total <= 0) {
    tft.fillRect(x, y, w, h, TFT_CYAN);
    return;
  }

  int thumbH = max(10, (h * BT_VISIBLE) / total);
  int maxStart = total - BT_VISIBLE;
  int thumbY = y + ((h - thumbH) * start) / maxStart;

  tft.fillRect(x, thumbY, w, thumbH, TFT_CYAN);
}

static int bt_findIndexByMAC(const String& mac) {
  for (int i = 0; i < (int)bt_rows.size(); i++) {
    if (bt_rows[i].mac == mac) return i;
  }
  return -1;
}

static void bt_drawList() {
  drawSkinHeader("BLE MAC Monitor", "MACs: " + String((int)bt_rows.size()));
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(BT_COL_MAC_X,  BT_HDR_Y); tft.print("MAC");
  tft.setCursor(BT_COL_RSSI_X, BT_HDR_Y); tft.print("RSSI");

  int total = (int)bt_rows.size();
  int start = bt_scroll;
  int end   = min(start + BT_VISIBLE, total);

  for (int row = 0; row < (end - start); row++) {
    int i = start + row;
    int y = BT_ROW0_Y + row * BT_ROW_STEP;

    if (i == bt_sel) {
      tft.fillRoundRect(5, y - BT_HI_PAD_Y, W - 10, 16, 4, COLOR_SELECTED_BG);
      tft.setTextColor(TFT_YELLOW);
    } else {
      if (bt_rows[i].name.length()) tft.setTextColor(TFT_GREEN);
      else tft.setTextColor(TFT_WHITE);
    }

    tft.setTextSize(1);
    tft.setCursor(BT_COL_MAC_X, y);
    tft.print(bt_rows[i].listLabel);

    tft.setCursor(BT_COL_RSSI_X, y);
    tft.print(bt_rows[i].rssi);
  }

  bt_drawScrollBar(total, bt_scroll);
  drawSkinFooter("LB:Exit   U/D:Select   OK:Details");
}

static void bt_drawDetailFull() {
  String badge = (bt_detail.mac.length() ? (String(bt_detail.rssi) + " dBm") : String("N/A"));
  drawSkinHeader("MAC Details", badge, TFT_CYAN);
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);

  tft.setCursor(BT_DET_X_LABEL, BT_DET_Y_NAME);
  tft.print("Name: ");
  if (bt_detail.name.length()) tft.print(tn_fitText(bt_detail.name, 16));
  else tft.print("N/A");

  tft.setCursor(BT_DET_X_LABEL, BT_DET_Y_MAC);
  tft.print("MAC: ");
  tft.print(bt_detail.mac);

  tft.setCursor(BT_DET_X_LABEL, BT_DET_Y_RSSI);
  tft.print("RSSI: ");
  tft.print(bt_detail.rssi);
  tft.print(" dBm");

  int y = BT_DET_Y_RSSI + 26;
  tft.setTextSize(1);

  auto printKV = [&](const String& k, const String& v) {
    if (y > (UI_FOOTER_Y - 12)) return;
    tft.setCursor(BT_DET_X_LABEL, y);
    tft.setTextColor(TFT_CYAN);
    tft.print(k);
    tft.setTextColor(TFT_WHITE);
    tft.print(v);
    y += 12;
  };

  if (bt_detail.haveTX)         printKV("TX: ",        String(bt_detail.tx) + " dBm");
  if (bt_detail.haveAppearance) printKV("Appearance: ", tn_hex16(bt_detail.appearance));
  if (bt_detail.haveMfg)        printKV("Mfg Data: ",  "Present");

  drawSkinFooter("Press any button to return");
  bt_lastDetailRSSI = bt_detail.rssi;
}

static void bt_tryResolveNameForDetail() {
  (void)bt_detail;
  return;
}

static void bt_updateDetailRSSIOnly() {
  if (bt_detail.rssi == bt_lastDetailRSSI) return;

  tft.fillRoundRect(200, UI_MAIN_HDR_Y + 10, 110, 25, 4, TFT_CYAN);
  tft.setTextColor(TFT_BLACK, TFT_CYAN);
  tft.setTextSize(1);
  String status = String(bt_detail.rssi) + " dBm";
  int textWidth = status.length() * 6;
  tft.setCursor(200 + (110 - textWidth) / 2, UI_MAIN_HDR_Y + 18);
  tft.print(status);

  tft.fillRect(BT_DET_RSSI_VAL_X, BT_DET_Y_RSSI - 2, BT_DET_RSSI_CLEAR_W, 18, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(BT_DET_RSSI_VAL_X, BT_DET_Y_RSSI);
  tft.print(bt_detail.rssi);
  tft.print(" dBm");

  bt_lastDetailRSSI = bt_detail.rssi;
}

static String bt_readDeviceNameOverGATT(const String& mac) {
  String out = "";
  BLEClient* client = BLEDevice::createClient();
  if (!client) return out;
  BLEAddress addr(mac.c_str());
  if (!client->connect(addr)) { client->disconnect(); delete client; return out; }
  BLERemoteService* ga = client->getService(BLEUUID((uint16_t)0x1800));
  if (!ga) { client->disconnect(); delete client; return out; }
  BLERemoteCharacteristic* dn = ga->getCharacteristic(BLEUUID((uint16_t)0x2A00));
  if (!dn) { client->disconnect(); delete client; return out; }
  std::string v = dn->readValue();
  if (v.size()) out = String(v.c_str());
  client->disconnect();
  delete client;
  return out;
}

static void bt_scanNowAndUpdate() {
  if (!bt_inited) return;

  BLEScan* scanner = BLEDevice::getScan();
  scanner->setActiveScan(true);

  tn_log("BLE", "Active BLE scan start (1s)");
  BLEScanResults res = scanner->start(1, false);
  tn_log("BLE", String("Active BLE scan complete, devices=") + String(res.getCount()));

  std::vector<BLERow> rows;
  rows.reserve(res.getCount());

  for (int i = 0; i < res.getCount(); i++) {
    BLEAdvertisedDevice d = res.getDevice(i);

    BLERow r;
    r.mac  = String(d.getAddress().toString().c_str());
    r.rssi = d.getRSSI();

    String _nm = String(d.getName().c_str());
    if (_nm.length()) {
      r.name      = _nm;
      r.listLabel = tn_fitText(r.name, BT_LIST_LABEL_MAX_CHARS);
    } else {
      r.name      = "";
      r.listLabel = tn_fitText(r.mac, BT_LIST_LABEL_MAX_CHARS);
    }

    if (d.haveTXPower())          { r.haveTX = true; r.tx = d.getTXPower(); }
    if (d.haveAppearance())       { r.haveAppearance = true; r.appearance = d.getAppearance(); }
    if (d.haveServiceUUID())      { r.haveSvcUUID = true; }
    if (d.haveManufacturerData()) { r.haveMfg = true; (void)d.getManufacturerData(); }

    rows.push_back(r);
  }

  std::sort(rows.begin(), rows.end(),
    [](const BLERow& a, const BLERow& b) { return a.rssi > b.rssi; });

  bt_rows.swap(rows);

  int total = (int)bt_rows.size();
  if (bt_sel >= total) bt_sel = total ? total - 1 : 0;
  if (bt_sel < 0) bt_sel = 0;

  if (bt_sel < bt_scroll) bt_scroll = bt_sel;
  if (bt_sel >= bt_scroll + BT_VISIBLE) bt_scroll = bt_sel - (BT_VISIBLE - 1);
  if (bt_scroll < 0) bt_scroll = 0;

  if (bt_detailOn && bt_selMAC.length()) {
    int idx = bt_findIndexByMAC(bt_selMAC);
    if (idx >= 0) {
      bt_detail = bt_rows[idx];
      bt_updateDetailRSSIOnly();
    }
  } else {
    bt_drawList();
  }
}

static void bt_scanTask(void* arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bt_scanBusy = true;
    bt_scanNowAndUpdate();
    bt_scanBusy = false;
  }
}

static void bt_enter() {
  if (!bt_inited) {
    BLEDevice::init("");
    bt_inited = true;
    tn_log("BLE", "BLEDevice initialized for monitor");
  }

  if (!bt_scanTaskHandle) {
    xTaskCreate(bt_scanTask, "btScan", 4096, NULL, 1, &bt_scanTaskHandle);
    tn_log("BLE", "BLE scan task created");
  }

  bt_rows.clear();
  bt_sel    = 0;
  bt_scroll = 0;

  bt_detailOn       = false;
  bt_selMAC         = "";
  bt_lastDetailRSSI = 9999;

  bt_lastScanMs       = 0;
  bt_lastDetailScanMs = 0;
  bt_lastKeyMs        = 0;
  bt_drawList();
  tn_logAppEnter(APP_BT_MONITOR);
  tn_log("BLE", "BLE MAC Monitor ready");
}

static void bt_loop() {
  if (bt_detailOn) {
    if (keys.now != 0) {
      bt_detailOn = false;
      bt_drawList();
    }
  } else {
    int total = (int)bt_rows.size();

    if (keys.pressed(MASK_JOY_UP)) {
      if (total > 0 && bt_sel > 0) bt_sel--;
      if (bt_sel < bt_scroll) bt_scroll = bt_sel;
      bt_drawList();
    }

    if (keys.pressed(MASK_JOY_DOWN)) {
      if (total > 0 && bt_sel < total - 1) bt_sel++;
      if (bt_sel >= bt_scroll + BT_VISIBLE) bt_scroll = bt_sel - (BT_VISIBLE - 1);
      bt_drawList();
    }

    if (keys.pressed(MASK_JOY_OK) && total > 0) {
      tn_log("BLE", String("MAC detail open: ") + bt_rows[bt_sel].mac);
      bt_selMAC   = bt_rows[bt_sel].mac;
      bt_detail   = bt_rows[bt_sel];
      bt_detailOn = true;
      bt_drawDetailFull();
      bt_tryResolveNameForDetail();
    }

    if (keys.pressed(MASK_BTN_C)) {
      tn_logAppExit(APP_BT_MONITOR);
      app = APP_MENU;
      menu_draw();
      tn_logAppEnter(APP_MENU);
    }
  }

  uint32_t now = millis();

  bool allowScan = !(bt_lastKeyMs != 0 && (now - bt_lastKeyMs) < BT_KEY_IDLE_MS);

  if (allowScan) {
    uint32_t  period = bt_detailOn ? BT_DETAIL_SCAN_PERIOD_MS : BT_SCAN_PERIOD_MS;
    uint32_t* lastp  = bt_detailOn ? &bt_lastDetailScanMs     : &bt_lastScanMs;
    if (*lastp == 0 || (now - *lastp) >= period) {
      *lastp = now;
      if (bt_scanTaskHandle && !bt_scanBusy) xTaskNotifyGive(bt_scanTaskHandle);
    }
  }

  delay(10);
}

// ===================== BLE MAC SCANNER =====================
struct BTScanRow {
  String mac;
  int    rssi;

  String name;
  String listLabel;
};

static std::vector<BTScanRow> bts_rows;
static int  bts_sel        = 0;
static int  bts_scroll     = 0;
static bool bts_showDetail = false;

static bool bts_inited = false;

static const int BTS_VISIBLE = 8;

static const int BTS_COL_MAC_X  = 15;
static const int BTS_COL_RSSI_X = 250;

static const int BTS_HDR_Y    = 64;
static const int BTS_ROW0_Y   = 86;
static const int BTS_ROW_STEP = 18;
static const int BTS_HI_PAD_Y = 4;

static const int BTS_LIST_LABEL_MAX_CHARS = 20;

static const int BTS_DET_X_LABEL = 10;
static const int BTS_DET_Y_NAME  = 72;
static const int BTS_DET_Y_MAC   = 102;
static const int BTS_DET_Y_RSSI  = 132;

static void bts_drawScrollBar(int total, int start) {
  const int x = 308;
  const int y = BTS_ROW0_Y - 14;
  const int h = (BTS_VISIBLE * BTS_ROW_STEP);
  const int w = 4;

  tft.fillRect(x, y, w, h, 0x2104);

  if (total <= BTS_VISIBLE || total <= 0) {
    tft.fillRect(x, y, w, h, TFT_CYAN);
    return;
  }

  int thumbH = max(10, (h * BTS_VISIBLE) / total);
  int maxStart = total - BTS_VISIBLE;
  int thumbY = y + ((h - thumbH) * start) / maxStart;

  tft.fillRect(x, thumbY, w, thumbH, TFT_CYAN);
}

static void bts_drawBodyHeader() {
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(BTS_COL_MAC_X,  BTS_HDR_Y); tft.print("MAC");
  tft.setCursor(BTS_COL_RSSI_X, BTS_HDR_Y); tft.print("RSSI");
}

static void bts_drawScanning() {
  drawSkinHeader("BLE MAC Scanner");
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(95, 119);
  tft.print("SCANNING...");

  drawSkinFooter("Please wait..");
}

static void bts_draw() {
  if (bts_showDetail) {
    BTScanRow& d = bts_rows[bts_sel];

    drawSkinHeader("MAC Details");

    tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);

    tft.setCursor(BTS_DET_X_LABEL, BTS_DET_Y_NAME);
    tft.print("Name: ");
    if (d.name.length()) tft.print(tn_fitText(d.name, 16));
    else                 tft.print("N/A");

    tft.setCursor(BTS_DET_X_LABEL, BTS_DET_Y_MAC);
    tft.print("MAC: ");
    tft.print(d.mac);

    tft.setCursor(BTS_DET_X_LABEL, BTS_DET_Y_RSSI);
    tft.print("RSSI: ");
    tft.print(d.rssi);
    tft.print(" dBm");

    drawSkinFooter("Press any button to return");
    return;
  }

  drawSkinHeader("BLE MAC Scanner", "MACs: " + String((int)bts_rows.size()));
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  bts_drawBodyHeader();

  int total = (int)bts_rows.size();
  int start = bts_scroll;
  int end   = min(start + BTS_VISIBLE, total);

  for (int row = 0; row < (end - start); row++) {
    int i = start + row;
    int y = BTS_ROW0_Y + row * BTS_ROW_STEP;

    if (i == bts_sel) {
      tft.fillRoundRect(5, y - BTS_HI_PAD_Y, W - 10, 16, 4, COLOR_SELECTED_BG);
      tft.setTextColor(TFT_YELLOW);
    } else {
      if (bts_rows[i].name.length()) tft.setTextColor(TFT_GREEN);
      else                           tft.setTextColor(TFT_WHITE);
    }

    tft.setTextSize(1);

    tft.setCursor(BTS_COL_MAC_X, y);
    tft.print(bts_rows[i].listLabel);

    tft.setCursor(BTS_COL_RSSI_X, y);
    tft.print(bts_rows[i].rssi);
  }

  bts_drawScrollBar(total, bts_scroll);
  drawSkinFooter("LB:Exit   RB:Scan   U/D:Select   OK:Details");
}

static void bts_scanOnce() {
  bts_drawScanning();

  bts_rows.clear();

  if (!bts_inited) {
    BLEDevice::init("");
    bts_inited = true;
    tn_log("BLE", "BLEDevice initialized for scanner");
  }

  BLEScan* scanner = BLEDevice::getScan();
  scanner->setActiveScan(false);

  tn_log("BLE", "Passive BLE scan start (3s)");
  BLEScanResults res = scanner->start(3, false);
  tn_log("BLE", String("Passive BLE scan complete, devices=") + String(res.getCount()));

  bts_rows.reserve(res.getCount());
  for (int i = 0; i < res.getCount(); i++) {
    BLEAdvertisedDevice d = res.getDevice(i);

    BTScanRow r;
    r.mac  = String(d.getAddress().toString().c_str());
    r.rssi = d.getRSSI();

    String _nm = String(d.getName().c_str());
    if (_nm.length()) {
      r.name      = _nm;
      r.listLabel = tn_fitText(r.name, BTS_LIST_LABEL_MAX_CHARS);
    } else {
      r.name      = "";
      r.listLabel = tn_fitText(r.mac, BTS_LIST_LABEL_MAX_CHARS);
    }

    bts_rows.push_back(r);
  }

  std::sort(bts_rows.begin(), bts_rows.end(),
    [](const BTScanRow& a, const BTScanRow& b) { return a.rssi > b.rssi; });

  bts_sel = 0;
  bts_scroll = 0;
  bts_showDetail = false;
  bts_draw();
}

static void bts_enter() {
  bts_rows.clear();
  bts_sel = 0;
  bts_scroll = 0;
  bts_showDetail = false;

  drawSkinHeader("BLE MAC Scanner");
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);
  drawSkinFooter("Please wait..");

  delay(1000);
  bts_scanOnce();
  tn_logAppEnter(APP_BT_SCAN);
  tn_log("BLE", "BLE MAC Scanner ready");
}

static void bts_loop() {
  if (bts_showDetail) {
    if (keys.now != 0) {
      bts_showDetail = false;
      bts_draw();
    }
    return;
  }

  int total = (int)bts_rows.size();

  if (keys.pressed(MASK_JOY_UP)) {
    if (total > 0 && bts_sel > 0) bts_sel--;
    if (bts_sel < bts_scroll) bts_scroll = bts_sel;
    bts_draw();
  }

  if (keys.pressed(MASK_JOY_DOWN)) {
    if (total > 0 && bts_sel < total - 1) bts_sel++;
    if (bts_sel >= bts_scroll + BTS_VISIBLE) bts_scroll = bts_sel - (BTS_VISIBLE - 1);
    bts_draw();
  }

  if (keys.pressed(MASK_JOY_OK) && total > 0) {
    bts_showDetail = true;
    bts_draw();
  }

  if (keys.pressed(MASK_BTN_A)) {
    bts_scanOnce();
    return;
  }

  if (keys.pressed(MASK_BTN_C)) {
    tn_logAppExit(APP_BT_SCAN);
    app = APP_MENU;
    menu_draw();
    tn_logAppEnter(APP_MENU);
  }
}

// ======= WIO BADUSB =======

#define KEY_NOT_FOUND 0x00

static uint8_t bu_lookupKey(const String& token) {
  // ---- Single-character printable tokens ----
  if (token.length() == 1) {
    char c = token[0];
    // Printable ASCII that Keyboard.press() accepts directly
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == ' ' || c == '!' || c == '"' || c == '#' || c == '$' ||
         c == '%' || c == '&' || c == '\'' || c == '(' || c == ')' ||
         c == '*' || c == '+' || c == ',' || c == '-' || c == '.' ||
         c == '/' || c == ':' || c == ';' || c == '<' || c == '=' ||
         c == '>' || c == '?' || c == '@' || c == '[' || c == '\\' ||
         c == ']' || c == '^' || c == '_' || c == '`' || c == '{' ||
         c == '|' || c == '}' || c == '~')) {
      return (uint8_t)c;
    }
  }

  // ---- Named key tokens (upper-cased before lookup) ----
  String t = token;
  t.toUpperCase();

  // Modifiers
  if (t == "CTRL"   || t == "CONTROL")                                return KEY_LEFT_CTRL;
  if (t == "RCTRL"  || t == "RIGHTCTRL"  || t == "RIGHTCONTROL")      return KEY_RIGHT_CTRL;
  if (t == "SHIFT")                                                    return KEY_LEFT_SHIFT;
  if (t == "RSHIFT" || t == "RIGHTSHIFT")                             return KEY_RIGHT_SHIFT;
  if (t == "ALT")                                                      return KEY_LEFT_ALT;
  if (t == "RALT"   || t == "RIGHTALT"   || t == "ALTGR")             return KEY_RIGHT_ALT;
  if (t == "GUI"    || t == "WINDOWS"    || t == "COMMAND" || t == "META") return KEY_LEFT_GUI;
  if (t == "RGUI"   || t == "RIGHTGUI"  || t == "RIGHTWINDOWS")       return KEY_RIGHT_GUI;

  // Navigation & editing
  if (t == "ENTER"     || t == "RETURN")                               return KEY_RETURN;
  if (t == "TAB")                                                       return KEY_TAB;
  if (t == "SPACE")                                                     return ' ';
  if (t == "BACKSPACE" || t == "BKSP")                                 return KEY_BACKSPACE;
  if (t == "DELETE"    || t == "DEL")                                  return KEY_DELETE;
  if (t == "INSERT"    || t == "INS")                                  return KEY_INSERT;
  if (t == "HOME")                                                      return KEY_HOME;
  if (t == "END")                                                       return KEY_END;
  if (t == "PAGEUP"    || t == "PAGE_UP"   || t == "PGUP")             return KEY_PAGE_UP;
  if (t == "PAGEDOWN"  || t == "PAGE_DOWN" || t == "PGDN")             return KEY_PAGE_DOWN;
  if (t == "ESCAPE"    || t == "ESC")                                  return KEY_ESC;

  // Arrow keys
  if (t == "UP"    || t == "UPARROW")    return KEY_UP_ARROW;
  if (t == "DOWN"  || t == "DOWNARROW")  return KEY_DOWN_ARROW;
  if (t == "LEFT"  || t == "LEFTARROW")  return KEY_LEFT_ARROW;
  if (t == "RIGHT" || t == "RIGHTARROW") return KEY_RIGHT_ARROW;

  // Lock keys
  if (t == "CAPSLOCK"   || t == "CAPS_LOCK")   return KEY_CAPS_LOCK;
  if (t == "NUMLOCK"    || t == "NUM_LOCK")    return KEY_NUM_LOCK;
  if (t == "SCROLLLOCK" || t == "SCROLL_LOCK") return KEY_SCROLL_LOCK;

  // System keys
  if (t == "PRINTSCREEN" || t == "PRINT_SCREEN" || t == "PRTSC") return KEY_PRINT_SCREEN;
  if (t == "PAUSE"  || t == "BREAK")                              return KEY_PAUSE;
  if (t == "MENU"   || t == "APP" || t == "CONTEXT_MENU")        return KEY_MENU;

  // Function keys F1–F24
  if (t == "F1")  return KEY_F1;
  if (t == "F2")  return KEY_F2;
  if (t == "F3")  return KEY_F3;
  if (t == "F4")  return KEY_F4;
  if (t == "F5")  return KEY_F5;
  if (t == "F6")  return KEY_F6;
  if (t == "F7")  return KEY_F7;
  if (t == "F8")  return KEY_F8;
  if (t == "F9")  return KEY_F9;
  if (t == "F10") return KEY_F10;
  if (t == "F11") return KEY_F11;
  if (t == "F12") return KEY_F12;
  if (t == "F13") return KEY_F13;
  if (t == "F14") return KEY_F14;
  if (t == "F15") return KEY_F15;
  if (t == "F16") return KEY_F16;
  if (t == "F17") return KEY_F17;
  if (t == "F18") return KEY_F18;
  if (t == "F19") return KEY_F19;
  if (t == "F20") return KEY_F20;
  if (t == "F21") return KEY_F21;
  if (t == "F22") return KEY_F22;
  if (t == "F23") return KEY_F23;
  if (t == "F24") return KEY_F24;

  // Numpad keys
  if (t == "NUM0"     || t == "KP0")       return KEY_KP_0;
  if (t == "NUM1"     || t == "KP1")       return KEY_KP_1;
  if (t == "NUM2"     || t == "KP2")       return KEY_KP_2;
  if (t == "NUM3"     || t == "KP3")       return KEY_KP_3;
  if (t == "NUM4"     || t == "KP4")       return KEY_KP_4;
  if (t == "NUM5"     || t == "KP5")       return KEY_KP_5;
  if (t == "NUM6"     || t == "KP6")       return KEY_KP_6;
  if (t == "NUM7"     || t == "KP7")       return KEY_KP_7;
  if (t == "NUM8"     || t == "KP8")       return KEY_KP_8;
  if (t == "NUM9"     || t == "KP9")       return KEY_KP_9;
  if (t == "NUMENTER" || t == "KP_ENTER")  return KEY_KP_ENTER;
  if (t == "NUMPLUS"  || t == "KP_PLUS")   return KEY_KP_PLUS;
  if (t == "NUMMINUS" || t == "KP_MINUS")  return KEY_KP_MINUS;
  if (t == "NUMDIV"   || t == "KP_DIV")    return KEY_KP_SLASH;
  if (t == "NUMDOT"   || t == "KP_DOT")    return KEY_KP_DOT;

  return KEY_NOT_FOUND;
}

// =====================================================================
// Parse a space-delimited token list and press all keys simultaneously,
// then release all. This gives unlimited chord support.
// Returns false if no valid keys were found.
// =====================================================================
static bool bu_pressChord(const String& tokenLine) {
  std::vector<uint8_t> codes;
  String remaining = tokenLine;
  remaining.trim();

  while (remaining.length() > 0) {
    int    sp = remaining.indexOf(' ');
    String token;
    if (sp == -1) {
      token     = remaining;
      remaining = "";
    } else {
      token     = remaining.substring(0, sp);
      remaining = remaining.substring(sp + 1);
      remaining.trim();
    }
    token.trim();
    if (token.length() == 0) continue;

    uint8_t code = bu_lookupKey(token);
    if (code != KEY_NOT_FOUND) codes.push_back(code);
  }

  if (codes.empty()) return false;

  for (uint8_t k : codes) Keyboard.press(k);
  delay(50);
  Keyboard.releaseAll();
  return true;
}

// ============
// BADUSB STATE
// ============
static bool   bu_sd     = false;
static String bu_dir    = "/";
static String bu_f[15];
static bool   bu_isDir[15];
static int    bu_c = 0, bu_s = 0;
static int    bu_scroll = 0;

static String bu_status     = "READY";
static String bu_msg        = "";
static bool   bu_msgIsError = false;

static void bu_setStatus(const String& st, const String& msg = "") {
  bu_status     = st;
  bu_msg        = msg;
  bu_msgIsError = (st == "ERROR");
}

static void bu_printWrapped(int x, int y, int maxW, const String& s, int maxLines) {
  const int approxChars = max(10, maxW / 6);
  int lineCount = 0;
  int i = 0;
  while (i < (int)s.length() && lineCount < maxLines) {
    int    end   = min(i + approxChars, (int)s.length());
    int    space = s.lastIndexOf(' ', end);
    if (space <= i) space = end;
    String line  = s.substring(i, space);
    line.trim();
    tft.setCursor(x, y + lineCount * 10);
    tft.print(line);
    i = space;
    while (i < (int)s.length() && s[i] == ' ') i++;
    lineCount++;
  }
}

static String bu_parentDir(const String& path) {
  if (path == "/" || path.length() == 0) return "/";
  String p = path;
  if (p.endsWith("/") && p.length() > 1) p.remove(p.length() - 1);
  int last = p.lastIndexOf('/');
  if (last <= 0) return "/";
  p = p.substring(0, last);
  if (p.length() == 0) p = "/";
  return p;
}

static bool bu_loadDir(const String& path, String* errOut = nullptr) {
  bu_c = 0;
  File r = SD.open(path.c_str());
  if (!r) {
    if (errOut) *errOut = "Can\x27t open folder: " + path;
    return false;
  }
  if (path != "/") {
    bu_f[bu_c]     = "Back";
    bu_isDir[bu_c] = true;
    bu_c++;
  }
  while (bu_c < 15) {
    File   e    = r.openNextFile();
    if (!e) break;
    String name = String(e.name());
    int pfx = name.indexOf("://");
    if (pfx != -1) name = name.substring(pfx + 3);
    if (name.startsWith("/")) name = name.substring(1);
    int ls = name.lastIndexOf("/");
    if (ls != -1) name = name.substring(ls + 1);
    if (name == "." || name == "Back") { e.close(); continue; }

    if (e.isDirectory()) {
      bu_f[bu_c]     = name;
      bu_isDir[bu_c] = true;
      bu_c++;
    } else if (name.endsWith(".txt")) {
      bu_f[bu_c]     = name;
      bu_isDir[bu_c] = false;
      bu_c++;
    }
    e.close();
  }
  r.close();
  return true;
}

static void bu_draw(const String& stOverride = "", const String& msgOverride = "") {
  if (stOverride.length()) bu_setStatus(stOverride, msgOverride);

  uint16_t col = (bu_status == "READY")   ? TFT_CYAN
               : (bu_status == "RUNNING") ? TFT_YELLOW
               : (bu_status == "ERROR")   ? TFT_RED
               :                            TFT_GREEN;

  drawSkinHeader("Wio BadUSB", bu_status, col);
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(15, 72);
  tft.print("SELECT PAYLOAD:");

  const int visible = 5;
  int start = bu_scroll;
  int end   = min(start + visible, bu_c);

  for (int row = 0; row < (end - start); row++) {
    int i = start + row;
    int y = 102 + row * 22;
    if (i == bu_s) {
      tft.fillRoundRect(10, y - 6, 300, 20, 4, COLOR_SELECTED_BG);
      tft.setTextColor(TFT_YELLOW);
    } else {
      tft.setTextColor(TFT_WHITE);
    }
    tft.setTextSize(1);
    tft.setCursor(25, y);
    if (bu_isDir[i]) tft.print("> ");
    else             tft.print("");
    tft.print(bu_f[i]);
  }

  if (bu_msg.length()) {
    tft.setTextSize(1);
    tft.setTextColor(bu_msgIsError ? TFT_RED : TFT_LIGHTGREY);
    tft.fillRect(10, 198, W - 20, 16, TFT_BLACK);
    bu_printWrapped(12, 200, W - 24, bu_msg, 1);
  }

  drawSkinFooter("LB:Back/Exit   U/D:Select   OK:Open/Run");
}

static void bu_runFile(const String& fullPath) {
  File f = SD.open(fullPath.c_str());
  if (!f) {
    tn_log("BADUSB", String("ERROR opening payload: ") + fullPath);
    bu_draw("ERROR", "Can\x27t open file: " + fullPath);
    return;
  }

  tn_log("BADUSB", String("Run payload: ") + fullPath);

  int    lc           = 0;   // line counter (for display)
  int    defaultDelay = 0;   // inter-command delay in ms
  String lastCmd      = "";  // for REPEAT
  String lastArg      = "";

  auto execCmd = [&](String cu, String a) {
    if (cu == "STRING") {
      Keyboard.print(a);
    } else if (cu == "STRINGLN") {
      Keyboard.print(a);
      Keyboard.write(KEY_RETURN);
    } else if (cu == "DELAY") {
      delay((uint32_t)a.toInt());
    } else if (cu == "DEFAULTDELAY" || cu == "DEFAULT_DELAY") {
      defaultDelay = a.toInt();
    } else {
      String chord = cu;
      if (a.length() > 0) { chord += " "; chord += a; }
      if (!bu_pressChord(chord)) {
        bu_draw("ERROR", "Unknown key (line " + String(lc) + "): " + cu);
        bu_status = "RUNNING";
      }
    }
  };

  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l == "" || l.startsWith("REM") || l.startsWith("//")) continue;

    int    sp = l.indexOf(' ');
    String cu = (sp == -1) ? l              : l.substring(0, sp);
    String a  = (sp == -1) ? ""             : l.substring(sp + 1);
    a.trim();

    String cuUp = cu;
    cuUp.toUpperCase();

    tn_log("BADUSB", String("Line ") + String(lc + 1) + " cmd=" + cuUp + (a.length() ? (" arg=" + a) : ""));

    bu_draw("RUNNING", "Line " + String(++lc) + ": " + cu);

    if (cuUp == "REPEAT") {
      int n = a.toInt();
      if (n < 1) n = 1;
      for (int r = 0; r < n; r++) {
        execCmd(lastCmd, lastArg);
        if (defaultDelay > 0) delay(defaultDelay);
      }
      continue;
    }

    execCmd(cuUp, a);

    lastCmd = cuUp;
    lastArg = a;

    if (defaultDelay > 0) delay(defaultDelay);
  }

  f.close();
  tn_log("BADUSB", String("Payload complete: ") + fullPath);
  bu_draw("DONE");
  delay(1500);
  bu_draw("READY", "");
}

// =====================================================================
// APP ENTRY & LOOP
// =====================================================================
static void bu_enter() {
  tn_logAppEnter(APP_BADUSB);
  Keyboard.begin();
  bu_setStatus("READY", "");
  if (SD.begin(SDCARD_SS_PIN, SDCARD_SPI)) {
    bu_sd  = true;
    bu_dir = "/";
    String err;
    if (!bu_loadDir(bu_dir, &err)) {
      bu_s = 0; bu_scroll = 0;
      bu_draw("ERROR", err);
      return;
    }
  } else {
    bu_sd = false;
  }
  bu_s = 0; bu_scroll = 0;
  if (!bu_sd) bu_draw("ERROR", "SD card not detected. Insert SD card and restart.");
  else        bu_draw("READY", "");
  if (bu_sd) tn_log("BADUSB", String("SD ready, dir=") + bu_dir);
}

static void bu_loop() {
  const int visible = 5;

  if (keys.pressed(MASK_JOY_UP)) {
    if (bu_s > 0) bu_s--;
    if (bu_s < bu_scroll) bu_scroll = bu_s;
    bu_draw();
  }
  if (keys.pressed(MASK_JOY_DOWN)) {
    if (bu_s < bu_c - 1) bu_s++;
    if (bu_s >= bu_scroll + visible) bu_scroll = bu_s - (visible - 1);
    bu_draw();
  }

  if (keys.pressed(MASK_BTN_C)) {
    if (!bu_sd) {
      tn_logAppExit(APP_BADUSB);
      app = APP_MENU;
      menu_draw();
      tn_logAppEnter(APP_MENU);
      return;
    }
    if (bu_dir != "/") {
      bu_dir = bu_parentDir(bu_dir);
      String err;
      if (!bu_loadDir(bu_dir, &err)) { bu_draw("ERROR", err); return; }
      bu_s = 0; bu_scroll = 0;
      bu_draw();
    } else {
      tn_logAppExit(APP_BADUSB);
      app = APP_MENU;
      menu_draw();
      tn_logAppEnter(APP_MENU);
    }
  }

  if (keys.pressed(MASK_JOY_OK) && bu_sd && bu_c > 0) {
    String name = bu_f[bu_s];

    if (bu_isDir[bu_s]) {
      if (name == "Back") {
        bu_dir = bu_parentDir(bu_dir);
      } else {
        bu_dir = (bu_dir == "/") ? "/" + name : bu_dir + "/" + name;
      }
      String err;
      if (!bu_loadDir(bu_dir, &err)) { bu_draw("ERROR", err); return; }
      bu_s = 0; bu_scroll = 0;
      bu_draw();
    } else {
      String fullPath;
      if (bu_dir == "/") fullPath = "/" + name;
      else               fullPath = bu_dir + "/" + name;

      int pfx = fullPath.indexOf("://");
      if (pfx != -1) fullPath = fullPath.substring(pfx + 3);
      if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;

      bu_runFile(fullPath);
    }
  }
}

// ===================== WIO CHAT =====================
IPAddress wc_apIP(192, 168, 4, 1);
DNSServer wc_dnsServer;
WebServer wc_server(80);

static bool     wc_chatOn       = false;
static bool     wc_stoppedFlash = false;
static uint32_t wc_stoppedUntil = 0;
static String   wc_apPassword   = "";
static String   wc_badgeText    = "READY";

#define WC_MAX_MSG 20
static String wc_messages[WC_MAX_MSG];
static int    wc_msgCount = 0;

static void wc_addMessage(String m) {
  if (wc_msgCount < WC_MAX_MSG) {
    wc_messages[wc_msgCount++] = m;
  } else {
    for (int i = 1; i < WC_MAX_MSG; i++) wc_messages[i - 1] = wc_messages[i];
    wc_messages[WC_MAX_MSG - 1] = m;
  }
}

static String wc_htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if      (c == '&')  out += F("&amp;");
    else if (c == '<')  out += F("&lt;");
    else if (c == '>')  out += F("&gt;");
    else if (c == '"')  out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else                out += c;
  }
  return out;
}

static void wc_sendNoCacheHeaders() {
  wc_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  wc_server.sendHeader("Pragma",  "no-cache");
  wc_server.sendHeader("Expires", "0");
}

static String wc_getMessagesHTML() {
  String out = "";
  for (int i = 0; i < wc_msgCount; i++) {
    out += "<div class='m'>" + wc_htmlEscape(wc_messages[i]) + "</div>";
  }
  return out;
}

static void wc_handleRoot() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>Wio Chat</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif; background:#0b0f14; color:#e6edf3; }
  .top { position:sticky; top:0; background:linear-gradient(180deg,#0e141b,#0b0f14); padding:14px 14px 10px; border-bottom:1px solid #1f2a37; }
  .title { font-size:20px; font-weight:700; letter-spacing:.3px; }
  .sub { font-size:12px; color:#9fb0c0; margin-top:4px; }
  .wrap { padding:12px 12px 84px; }
  #chat { display:flex; flex-direction:column; gap:8px; }
  .m { background:#101826; border:1px solid #1f2a37; border-radius:12px; padding:10px 12px; line-height:1.25; word-wrap:break-word; }
  .bar { position:fixed; left:0; right:0; bottom:0; background:#0b0f14; border-top:1px solid #1f2a37; padding:10px; display:flex; gap:8px; }
  .bar input { background:#0f1722; border:1px solid #263447; color:#e6edf3; border-radius:10px; padding:10px 10px; outline:none; }
  #name { width:90px; }
  #msg { flex:1; }
  button { background:#1f6feb; border:1px solid #1f6feb; color:white; border-radius:10px; padding:10px 12px; font-weight:700; }
  button:disabled { opacity:.5; }
</style>
</head>
<body>
  <div class='top'>
    <div class='title'>Wio Chat</div>
    <div class='sub'>Offline local chat room (Wio Terminal AP)</div>
  </div>

  <div class='wrap'>
    <div id='chat'></div>
  </div>

  <div class='bar'>
    <input id='name' value='User' maxlength='16' autocomplete='off'>
    <input id='msg' placeholder='Message...' maxlength='140' autocomplete='off'>
    <button id='sendBtn' onclick='sendMsg()'>Send</button>
  </div>

<script>
  var busy = false;

  function appendHTML(html){
    if(!html) return;
    var chat = document.getElementById('chat');
    var stick = (window.innerHeight + window.scrollY) >= (document.body.offsetHeight - 120);
    chat.innerHTML = html;
    if(stick){ window.scrollTo(0, document.body.scrollHeight); }
  }

  function poll(){
    if(busy) return;
    busy = true;
    fetch('/msgs?_=' + Date.now(), { cache: 'no-store' })
      .then(function(r){ return r.text(); })
      .then(function(html){ busy = false; appendHTML(html); })
      .catch(function(){ busy = false; });
  }

  function sendMsg(){
    var name = document.getElementById('name').value || 'User';
    var msg = document.getElementById('msg').value || '';
    msg = msg.trim();
    if(!msg) return;
    var payload = encodeURIComponent(name + ': ' + msg);

    document.getElementById('msg').value = '';
    fetch('/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      cache: 'no-store',
      body: 'm=' + payload + '&_=' + Date.now()
    }).then(function(){ poll(); });
  }

  function init(){
    var msg = document.getElementById('msg');
    msg.addEventListener('keydown', function(e){
      if(e.key === 'Enter'){ e.preventDefault(); sendMsg(); }
    });
    setInterval(poll, 700);
    poll();
  }

  window.addEventListener('load', init);
</script>
</body>
</html>
)rawliteral";
  wc_sendNoCacheHeaders();
  wc_server.send(200, "text/html", page);
}

static void wc_handleSend() {
  if (wc_server.hasArg("m")) {
    wc_addMessage(wc_server.arg("m"));
    tn_log("WIOCHAT", String("Message: ") + wc_server.arg("m"));
  }
  wc_sendNoCacheHeaders();
  wc_server.send(200, "text/plain", "OK");
}

static void wc_handleMsgs() {
  wc_sendNoCacheHeaders();
  wc_server.send(200, "text/html", wc_getMessagesHTML());
}

static void wc_handleNotFound() {
  wc_server.sendHeader("Location", "http://192.168.4.1/", true);
  wc_server.send(302, "text/plain", "");
}

static void wc_drawHeader(String text) {
  uint16_t color;
  if (text == "STOPPED") color = TFT_YELLOW;
  else if (wc_chatOn)    color = TFT_GREEN;
  else                   color = TFT_CYAN;
  drawSkinHeader("Wio Chat", text, color);
}

static void wc_drawFooter(String txt) {
  drawSkinFooter(txt);
}

static void wc_drawSwitch() {
  tft.fillRect(0, 72, W, 128, TFT_BLACK);

  const int pillW = 100, pillH = 34;
  int       px    = (W - pillW) / 2, py = 92;

  uint16_t col = wc_chatOn ? TFT_GREEN : COLOR_TOGGLE_OFF;
  tft.fillRoundRect(px, py, pillW, pillH, 17, col);

  const int knob = 26;
  int kx = wc_chatOn ? (px + pillW - 4 - knob) : (px + 4);
  int ky = py + (pillH - knob) / 2;
  tft.fillCircle(kx + knob / 2, ky + knob / 2, knob / 2, TFT_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, col);
  String s = wc_chatOn ? "ON" : "OFF";
  tft.setCursor(px + (pillW - (int)s.length() * 6) / 2, py + (pillH - 8) / 2);
  tft.print(s);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  String l1 = "Connect to Access Point: WioChat";
  String l2 = "Wi-Fi Password: code (top-right)";
  String l3 = "Go to http://WioChat.com";
  tft.setCursor((W - l1.length() * 6) / 2, 147); tft.print(l1);
  tft.setCursor((W - l2.length() * 6) / 2, 162); tft.print(l2);
  tft.setCursor((W - l3.length() * 6) / 2, 177); tft.print(l3);

  wc_drawFooter("LB:Stop/Exit   OK:Toggle");
}

static void wc_drawFullUI() {
  tft.fillScreen(TFT_BLACK);
  wc_drawHeader(wc_badgeText);
  wc_drawSwitch();
}

static String wc_generatePassword() {
  static const char chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "!@#$%^&*-_=+?.";
  const size_t n = sizeof(chars) - 1;
  String p = "";
  p.reserve(16);
  for (int i = 0; i < 16; i++) {
    p += chars[random(0, (int)n)];
  }
  return p;
}

static void wc_startChat() {
  wc_apPassword = wc_generatePassword();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(wc_apIP, wc_apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("WioChat", wc_apPassword.c_str());
  wc_dnsServer.start(53, "*", wc_apIP);
  wc_server.on("/",      wc_handleRoot);
  wc_server.on("/send",  wc_handleSend);
  wc_server.on("/msgs",  wc_handleMsgs);
  wc_server.onNotFound(wc_handleNotFound);
  wc_server.begin();
  wc_badgeText = wc_apPassword;
  wc_chatOn    = true;
  tn_log("WIOCHAT", String("AP started SSID=WioChat IP=") + wc_apIP.toString());
}

static void wc_stopChat() {
  wc_server.stop();
  wc_dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wc_chatOn       = false;
  wc_badgeText    = "STOPPED";
  wc_stoppedFlash = true;
  wc_stoppedUntil = millis() + 1500;
  tn_log("WIOCHAT", "AP stopped");
}

static void wc_enter() {
  randomSeed((uint32_t)micros() ^ (uint32_t)analogRead(0));
  wc_chatOn       = false;
  wc_stoppedFlash = false;
  wc_badgeText    = "READY";
  wc_msgCount     = 0;
  wc_drawFullUI();
  tn_logAppEnter(APP_WIOCHAT);
  tn_log("WIOCHAT", "App ready");
}

static void wc_loop() {
  if (wc_stoppedFlash && millis() > wc_stoppedUntil) {
    wc_stoppedFlash = false;
    wc_badgeText    = "READY";
    wc_drawFullUI();
  }

  if (keys.pressed(MASK_JOY_OK)) {
    if (!wc_chatOn) wc_startChat();
    else            wc_stopChat();
    wc_drawFullUI();
  }

  if (keys.pressed(MASK_BTN_C)) {
    if (wc_chatOn) {
      wc_stopChat();
      wc_drawFullUI();
    } else {
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      delay(50);
      tn_logAppExit(APP_WIOCHAT);
      app = APP_MENU;
      menu_draw();
      tn_logAppEnter(APP_MENU);
    }
  }

  if (wc_chatOn) {
    wc_dnsServer.processNextRequest();
    wc_server.handleClient();
  }
}

// ===================== WIO DROP =====================
IPAddress wd_apIP(192, 168, 4, 1);
DNSServer wd_dnsServer;
WebServer wd_server(80);

static bool     wd_on           = false;
static bool     wd_stoppedFlash = false;
static uint32_t wd_stoppedUntil = 0;
static String   wd_apPassword   = "";
static String   wd_badgeText    = "READY";
static bool     wd_sdOk         = false;

static File     wd_uploadFile;
static String   wd_uploadName   = "";

static void wd_sendNoCacheHeaders() {
  wd_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  wd_server.sendHeader("Pragma",  "no-cache");
  wd_server.sendHeader("Expires", "0");
}

static String wd_sanitizeFileName(String n) {
  n.trim();
  n.replace("\\", "/");
  int slash = n.lastIndexOf('/');
  if (slash >= 0) n = n.substring(slash + 1);

  String out;
  out.reserve(n.length());
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
      out += c;
    } else {
      out += '_';
    }
  }
  if (out.length() == 0) out = "file.bin";
  if (out.length() > 48) out.remove(48);
  return out;
}

static String wd_htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if      (c == '&')  out += F("&amp;");
    else if (c == '<')  out += F("&lt;");
    else if (c == '>')  out += F("&gt;");
    else if (c == '"')  out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else                out += c;
  }
  return out;
}

static void wd_handleRoot() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>Wio Drop</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif; background:#0b0f14; color:#e6edf3; }
  .top { position:sticky; top:0; background:linear-gradient(180deg,#0e141b,#0b0f14); padding:14px 14px 10px; border-bottom:1px solid #1f2a37; }
  .title { font-size:20px; font-weight:700; letter-spacing:.3px; }
  .sub { font-size:12px; color:#9fb0c0; margin-top:4px; }
  .wrap { padding:12px 12px 84px; }
  .card { background:#101826; border:1px solid #1f2a37; border-radius:12px; padding:12px; }
  .row { display:flex; gap:10px; align-items:center; flex-wrap:wrap; }
  button { background:#1f6feb; border:1px solid #1f6feb; color:white; border-radius:10px; padding:10px 12px; font-weight:700; }
  button:disabled { opacity:.55; }
  .hint { font-size:12px; color:#9fb0c0; margin-top:10px; line-height:1.25; }
  .files { margin-top:12px; display:flex; flex-direction:column; gap:8px; }
  .f { display:flex; justify-content:space-between; gap:10px; align-items:center; background:#0f1722; border:1px solid #263447; border-radius:10px; padding:10px 12px; }
  a { color:#79c0ff; text-decoration:none; }
  .small { font-size:12px; color:#9fb0c0; }
  progress { width:100%; height:14px; }
</style>
</head>
<body>
  <div class='top'>
    <div class='title'>Wio Drop</div>
    <div class='sub'>Offline file sharing.</div>
  </div>

  <div class='wrap'>
    <div class='card'>
      <div class='row'>
        <input id='file' type='file'>
        <button id='upBtn' onclick='upload()'>Upload</button>
      </div>
      <div id='pwrap' style='display:none; margin-top:10px;'>
        <div class='small' id='ptext'>Uploading...</div>
        <progress id='prog' value='0' max='100'></progress>
      </div>
      <div class='hint'>
        Tip: If file selection doesn't work, try http://WioDrop.com on Google Chrome or another brower.<br>
        Download links appear below—after refresh.
      </div>
    </div>

    <div class='files' id='files'></div>
  </div>

<script>
  function loadFiles(){
    fetch('/list?_=' + Date.now(), { cache: 'no-store' })
      .then(r => r.text())
      .then(html => { document.getElementById('files').innerHTML = html || ''; })
      .catch(()=>{});
  }

  function upload(){
    var f = document.getElementById('file').files[0];
    if(!f) return;

    var upBtn = document.getElementById('upBtn');
    var pwrap = document.getElementById('pwrap');
    var prog  = document.getElementById('prog');
    var ptext = document.getElementById('ptext');

    upBtn.disabled = true;
    pwrap.style.display = '';
    prog.value = 0;
    ptext.textContent = 'Uploading... do not disconnect or refresh';

    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/upload', true);

    xhr.upload.onprogress = function(e){
      if(e.lengthComputable){
        var pct = Math.floor((e.loaded / e.total) * 100);
        prog.value = pct;
        ptext.textContent = 'Uploading... ' + pct + '%';
      }
    };

    xhr.onreadystatechange = function(){
      if(xhr.readyState === 4){
        upBtn.disabled = false;
        ptext.textContent = (xhr.status === 200) ? 'Upload complete' : ('Upload failed (' + xhr.status + ')');
        setTimeout(function(){ pwrap.style.display = 'none'; }, 1200);
        loadFiles();
      }
    };

    var fd = new FormData();
    fd.append('f', f, f.name);
    xhr.send(fd);
  }

  window.addEventListener('load', loadFiles);
</script>
</body>
</html>
)rawliteral";

  wd_sendNoCacheHeaders();
  wd_server.send(200, "text/html", page);
}

static void wd_handleList() {
  wd_sendNoCacheHeaders();

  if (!wd_sdOk) {
    wd_server.send(200, "text/html", "<div class='card'>SD not available.</div>");
    return;
  }

  File dir = SD.open("/");
  if (!dir) {
    wd_server.send(200, "text/html", "<div class='card'>Can't open SD root.</div>");
    return;
  }

  String out = "";
  while (true) {
    File e = dir.openNextFile();
    if (!e) break;
    if (e.isDirectory()) { e.close(); continue; }

    String name = String(e.name());
    uint32_t size = (uint32_t)e.size();
    e.close();

    String s = String(size) + " B";
    if (size > 1024) s = String(size / 1024) + " KB";
    if (size > 1024UL * 1024UL) s = String(size / (1024UL * 1024UL)) + " MB";

    out += "<div class='f'><div><div><a href='/dl?f=" + name + "'>" + wd_htmlEscape(name) + "</a></div><div class='small'>" + s + "</div></div>";
    out += "<div><a href='/rm?f=" + name + "' onclick='return confirm(\"Delete " + wd_htmlEscape(name) + "?\")'>Delete</a></div></div>";
  }
  dir.close();

  if (out.length() == 0) out = "<div class='card'>No files on SD.</div>";
  wd_server.send(200, "text/html", out);
}

static void wd_handleDownload() {
  if (!wd_sdOk) { wd_server.send(500, "text/plain", "SD not available"); return; }
  if (!wd_server.hasArg("f")) { wd_server.send(400, "text/plain", "Missing f"); return; }

  String name = wd_sanitizeFileName(wd_server.arg("f"));
  String path = "/" + name;
  tn_log("WIODROP", String("Download request: ") + path);

  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) { wd_server.send(404, "text/plain", "Not found"); return; }

  wd_sendNoCacheHeaders();
  wd_server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  wd_server.streamFile(f, "application/octet-stream");
  f.close();
}

static void wd_handleDelete() {
  if (!wd_sdOk) { wd_server.send(500, "text/plain", "SD not available"); return; }
  if (!wd_server.hasArg("f")) { wd_server.send(400, "text/plain", "Missing f"); return; }

  String name = wd_sanitizeFileName(wd_server.arg("f"));
  String path = "/" + name;
  tn_log("WIODROP", String("Delete request: ") + path);

  if (SD.exists(path.c_str())) SD.remove(path.c_str());

  wd_sendNoCacheHeaders();
  wd_server.sendHeader("Location", "/", true);
  wd_server.send(302, "text/plain", "");
}

static void wd_handleUploadDone() {
  wd_sendNoCacheHeaders();
  wd_server.send(200, "text/plain", "OK");
}

static void wd_handleUploadStream() {
  HTTPUpload& up = wd_server.upload();

  if (up.status == UPLOAD_FILE_START) {
    if (!wd_sdOk) return;

    wd_uploadName = wd_sanitizeFileName(up.filename);
    String path = "/" + wd_uploadName;

    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    wd_uploadFile = SD.open(path.c_str(), FILE_WRITE);
    tn_log("WIODROP", String("Upload start: ") + wd_uploadName);
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (!wd_uploadFile) return;
    if (up.currentSize) {
      wd_uploadFile.write(up.buf, up.currentSize);
      tn_log("WIODROP", String("Upload chunk: ") + wd_uploadName + " +" + String(up.currentSize) + " bytes");
    }
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (wd_uploadFile) {
      wd_uploadFile.flush();
      wd_uploadFile.close();
      tn_log("WIODROP", String("Upload complete: ") + wd_uploadName);
    }
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    if (wd_uploadFile) wd_uploadFile.close();
    if (wd_uploadName.length()) {
      String path = "/" + wd_uploadName;
      if (SD.exists(path.c_str())) SD.remove(path.c_str());
    }
    tn_log("WIODROP", String("Upload aborted: ") + wd_uploadName);
  }
}

static void wd_fastRedirect() {
  wd_server.sendHeader("Location", "http://192.168.4.1/", true);
  wd_server.send(302, "text/plain", "");
}

static void wd_handleNotFound() { wd_fastRedirect(); }

static void wd_drawHeader(String text) {
  uint16_t color;
  if (text == "STOPPED") color = TFT_YELLOW;
  else if (wd_on)        color = TFT_GREEN;
  else                   color = TFT_CYAN;
  drawSkinHeader("Wio Drop", text, color);
}

static void wd_drawFooter(String txt) { drawSkinFooter(txt); }

static void wd_drawSwitch() {
  tft.fillRect(0, 72, W, 128, TFT_BLACK);

  const int pillW = 110, pillH = 34;
  int px = (W - pillW) / 2, py = 92;

  uint16_t col = wd_on ? TFT_GREEN : COLOR_TOGGLE_OFF;
  tft.fillRoundRect(px, py, pillW, pillH, 17, col);

  const int knob = 26;
  int kx = wd_on ? (px + pillW - 4 - knob) : (px + 4);
  int ky = py + (pillH - knob) / 2;
  tft.fillCircle(kx + knob / 2, ky + knob / 2, knob / 2, TFT_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, col);
  String s = wd_on ? "ON" : "OFF";
  tft.setCursor(px + (pillW - (int)s.length() * 6) / 2, py + (pillH - 8) / 2);
  tft.print(s);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  String l1 = "Connect to Access Point: WioDrop";
  String l2 = "Wi-Fi Password: code (top-right)";
  String l3 = "Go to http://WioDrop.com";
  tft.setCursor((W - l1.length() * 6) / 2, 147); tft.print(l1);
  tft.setCursor((W - l2.length() * 6) / 2, 162); tft.print(l2);
  tft.setCursor((W - l3.length() * 6) / 2, 177); tft.print(l3);

  String l4 = wd_sdOk ? "SD Status: OK" : "SD Status: ERROR";
  tft.setCursor((W - l4.length() * 6) / 2, 192); tft.print(l4);

  wd_drawFooter("LB:Stop/Exit   OK:Toggle");
}

static void wd_drawFullUI() {
  tft.fillScreen(TFT_BLACK);
  wd_drawHeader(wd_badgeText);
  wd_drawSwitch();
}

static String wd_generatePassword() {
  static const char chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "!@#$%^&*-_=+?.";
  const size_t n = sizeof(chars) - 1;
  String p = "";
  p.reserve(16);
  for (int i = 0; i < 16; i++) p += chars[random(0, (int)n)];
  return p;
}

static void wd_start() {
  wd_apPassword = wd_generatePassword();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(wd_apIP, wd_apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("WioDrop", wd_apPassword.c_str());

  wd_dnsServer.start(53, "*", wd_apIP);

  wd_server.on("/",      wd_handleRoot);
  wd_server.on("/list",  wd_handleList);
  wd_server.on("/dl",    wd_handleDownload);
  wd_server.on("/rm",    wd_handleDelete);
  wd_server.on("/upload", HTTP_POST, wd_handleUploadDone, wd_handleUploadStream);

  wd_server.on("/generate_204",        wd_fastRedirect);
  wd_server.on("/hotspot-detect.html", wd_fastRedirect);
  wd_server.on("/ncsi.txt",            wd_fastRedirect);
  wd_server.on("/connecttest.txt",     wd_fastRedirect);
  wd_server.on("/redirect",            wd_fastRedirect);

  wd_server.onNotFound(wd_handleNotFound);
  wd_server.begin();

  wd_badgeText = wd_apPassword;
  wd_on = true;
  tn_log("WIODROP", String("AP started SSID=WioDrop IP=") + wd_apIP.toString());
}

static void wd_stop() {
  wd_server.stop();
  wd_dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  wd_on           = false;
  wd_badgeText    = "STOPPED";
  wd_stoppedFlash = true;
  wd_stoppedUntil = millis() + 1500;
  tn_log("WIODROP", "AP stopped");
}

static void wd_enter() {
  randomSeed((uint32_t)micros() ^ (uint32_t)analogRead(0));

  wd_on           = false;
  wd_stoppedFlash = false;
  wd_badgeText    = "READY";

  wd_sdOk = SD.begin(SDCARD_SS_PIN, SDCARD_SPI);

  wd_drawFullUI();
  tn_logAppEnter(APP_WIODROP);
  tn_log("WIODROP", String("App ready, SD=") + (wd_sdOk ? "OK" : "ERROR"));
}

static void wd_loop() {
  if (wd_stoppedFlash && millis() > wd_stoppedUntil) {
    wd_stoppedFlash = false;
    wd_badgeText    = "READY";
    wd_drawFullUI();
  }

  if (keys.pressed(MASK_JOY_OK)) {
    if (!wd_on) wd_start();
    else        wd_stop();
    wd_drawFullUI();
  }

  if (keys.pressed(MASK_BTN_C)) {
    if (wd_on) {
      wd_stop();
      wd_drawFullUI();
    } else {
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      delay(50);
      tn_logAppExit(APP_WIODROP);
      app = APP_MENU;
      menu_draw();
      tn_logAppEnter(APP_MENU);
    }
  }

  if (wd_on) {
    wd_dnsServer.processNextRequest();
    wd_server.handleClient();
  }
}

// ===================== SD TOOL =====================

static bool   sdtool_sd_ok = false;
static String sdtool_dir   = "/";

static String sdtool_name[15];
static bool   sdtool_isDir[15];
static int    sdtool_c = 0, sdtool_s = 0, sdtool_scroll = 0;

static bool   sdtool_cb_has   = false;
static bool   sdtool_cb_cut   = false; // false=copy, true=move
static bool   sdtool_cb_isDir = false;
static String sdtool_cb_path  = "";

static String sdtool_status = "READY";
static String sdtool_msg    = "";
static bool   sdtool_err    = false;

static void sdtool_setStatus(const String& st, const String& msg = "") {
  sdtool_status = st;
  sdtool_msg    = msg;
  sdtool_err    = (st == "ERROR");
}

static String sdtool_parentDir(const String& path) {
  if (path == "/" || path.length() == 0) return "/";
  String p = path;
  if (p.endsWith("/") && p.length() > 1) p.remove(p.length() - 1);
  int last = p.lastIndexOf('/');
  if (last <= 0) return "/";
  p = p.substring(0, last);
  if (p.length() == 0) p = "/";
  return p;
}

static String sdtool_join(const String& dir, const String& name) {
  if (dir == "/") return "/" + name;
  return dir + "/" + name;
}

static String sdtool_basename(const String& p) {
  String s = p;
  if (s.endsWith("/") && s.length() > 1) s.remove(s.length() - 1);
  int ls = s.lastIndexOf('/');
  if (ls == -1) return s;
  return s.substring(ls + 1);
}

static String sdtool_normPath(String path) {
  int pfx = path.indexOf("://");
  if (pfx != -1) path = path.substring(pfx + 3);
  if (!path.startsWith("/")) path = "/" + path;
  while (path.indexOf("//") != -1) path.replace("//", "/");
  return path;
}

static bool sdtool_beginSD() {
  sdtool_sd_ok = SD.begin(SDCARD_SS_PIN, SDCARD_SPI);
  return sdtool_sd_ok;
}

static bool sdtool_loadDir(const String& path, String* errOut = nullptr) {
  sdtool_c = 0;

  if (!sdtool_sd_ok) {
    if (errOut) *errOut = "SD card not detected. Insert SD card and restart.";
    return false;
  }

  File r = SD.open(path.c_str());
  if (!r) {
    if (errOut) *errOut = "Can't open folder: " + path;
    return false;
  }

  if (path != "/") {
    sdtool_name[sdtool_c]  = "Back";
    sdtool_isDir[sdtool_c] = true;
    sdtool_c++;
  }

  while (sdtool_c < 15) {
    File e = r.openNextFile();
    if (!e) break;

    String name = String(e.name());
    int pfx = name.indexOf("://");
    if (pfx != -1) name = name.substring(pfx + 3);
    if (name.startsWith("/")) name = name.substring(1);
    int ls = name.lastIndexOf("/");
    if (ls != -1) name = name.substring(ls + 1);

    if (name == "." || name == "Back") { e.close(); continue; }

    sdtool_name[sdtool_c]  = name;
    sdtool_isDir[sdtool_c] = e.isDirectory();
    sdtool_c++;

    e.close();
  }

  r.close();
  return true;
}

static bool sdtool_rmTree(const String& pathIn, String* errOut = nullptr) {
  if (!sdtool_sd_ok) { if (errOut) *errOut = "SD card not detected. Insert SD card and restart."; return false; }

  String path = sdtool_normPath(pathIn);

  File f = SD.open(path.c_str());
  if (!f) { if (errOut) *errOut = "Not found: " + path; return false; }

  bool isDir = f.isDirectory();
  f.close();

  if (!isDir) {
    if (!SD.remove(path.c_str())) { if (errOut) *errOut = "Delete failed: " + path; return false; }
    return true;
  }

  File dir = SD.open(path.c_str());
  if (!dir) { if (errOut) *errOut = "Can't open: " + path; return false; }

  while (true) {
    File e = dir.openNextFile();
    if (!e) break;

    String child = sdtool_normPath(String(e.name()));
    e.close();

    if (!sdtool_rmTree(child, errOut)) { dir.close(); return false; }
  }
  dir.close();

  if (!SD.rmdir(path.c_str())) { if (errOut) *errOut = "Remove dir failed: " + path; return false; }
  return true;
}

static bool sdtool_copyFile(const String& srcIn, const String& dstIn, String* errOut = nullptr) {
  String src = sdtool_normPath(srcIn);
  String dst = sdtool_normPath(dstIn);

  File in = SD.open(src.c_str(), FILE_READ);
  if (!in) { if (errOut) *errOut = "Can't open src: " + src; return false; }

  if (SD.exists(dst.c_str())) SD.remove(dst.c_str());
  File out = SD.open(dst.c_str(), FILE_WRITE);
  if (!out) { in.close(); if (errOut) *errOut = "Can't open dst: " + dst; return false; }

  uint8_t buf[512];
  while (in.available()) {
    int n = in.read(buf, sizeof(buf));
    if (n <= 0) break;
    out.write(buf, n);
  }

  out.flush();
  out.close();
  in.close();
  return true;
}

static bool sdtool_copyDir(const String& srcIn, const String& dstIn, String* errOut = nullptr) {
  String src = sdtool_normPath(srcIn);
  String dst = sdtool_normPath(dstIn);

  if (!SD.exists(dst.c_str())) {
    if (!SD.mkdir(dst.c_str())) { if (errOut) *errOut = "mkdir failed: " + dst; return false; }
  }

  File dir = SD.open(src.c_str());
  if (!dir) { if (errOut) *errOut = "Can't open dir: " + src; return false; }

  while (true) {
    File e = dir.openNextFile();
    if (!e) break;

    String childFull = sdtool_normPath(String(e.name()));
    bool isDir = e.isDirectory();
    e.close();

    String base = sdtool_basename(childFull);
    String dstPath = (dst == "/") ? ("/" + base) : (dst + "/" + base);

    if (isDir) {
      if (!sdtool_copyDir(childFull, dstPath, errOut)) { dir.close(); return false; }
    } else {
      if (!sdtool_copyFile(childFull, dstPath, errOut)) { dir.close(); return false; }
    }
  }

  dir.close();
  return true;
}

static bool sdtool_pasteInto(const String& destDirIn, String* errOut = nullptr) {
  if (!sdtool_cb_has) { if (errOut) *errOut = "Clipboard empty."; return false; }
  if (!sdtool_sd_ok)  { if (errOut) *errOut = "SD card not detected. Insert SD card and restart."; return false; }

  String destDir = sdtool_normPath(destDirIn);
  String base    = sdtool_basename(sdtool_cb_path);
  String dst     = (destDir == "/") ? ("/" + base) : (destDir + "/" + base);

  if (sdtool_cb_isDir) {
    if (dst == sdtool_cb_path) { if (errOut) *errOut = "Same folder."; return false; }
    if (dst.startsWith(sdtool_cb_path + "/")) { if (errOut) *errOut = "Can't paste into itself."; return false; }
  }

  bool ok = false;
  if (sdtool_cb_isDir) ok = sdtool_copyDir(sdtool_cb_path, dst, errOut);
  else                 ok = sdtool_copyFile(sdtool_cb_path, dst, errOut);
  if (!ok) return false;

  if (sdtool_cb_cut) {
    if (!sdtool_rmTree(sdtool_cb_path, errOut)) return false;
    sdtool_cb_has = false;
    sdtool_cb_cut = false;
    sdtool_cb_isDir = false;
    sdtool_cb_path = "";
  }

  return true;
}

static void sdtool_draw() {
  uint16_t col = (sdtool_status == "READY") ? TFT_CYAN
               : (sdtool_status == "ERROR") ? TFT_RED
               :                              TFT_YELLOW;

  drawSkinHeader("SD Tool", sdtool_status, col);
  tft.fillRect(0, UI_CONTENT_Y, W, UI_CONTENT_H, TFT_BLACK);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(12, 64);
  tft.print("DIRECTORY: ");
  tft.print(sdtool_dir);

  tft.setCursor(12, 76);
  tft.setTextColor(TFT_LIGHTGREY);
  if (sdtool_cb_has) {
    tft.print("CLIPBOARD: ");
    tft.print(sdtool_cb_cut ? "MOVE " : "COPY ");
    tft.print(sdtool_basename(sdtool_cb_path));
  } else {
    tft.print("CLIPBOARD: <empty>");
  }

  const int visible = 5;
  int start = sdtool_scroll;
  int end   = min(start + visible, sdtool_c);

  for (int row = 0; row < (end - start); row++) {
    int i = start + row;
    int y = 100 + row * 22;

    if (i == sdtool_s) {
      tft.fillRoundRect(10, y - 6, 300, 20, 4, COLOR_SELECTED_BG);
      tft.setTextColor(TFT_YELLOW);
    } else {
      tft.setTextColor(TFT_WHITE);
    }

    tft.setTextSize(1);
    tft.setCursor(25, y);
    if (sdtool_isDir[i]) tft.print("> ");
    else                 tft.print("  ");
    tft.print(sdtool_name[i]);
  }

  if (sdtool_msg.length()) {
    tft.setTextSize(1);
    tft.setTextColor(sdtool_err ? TFT_RED : TFT_LIGHTGREY);
    tft.fillRect(10, 198, W - 20, 16, TFT_BLACK);
    tft.setCursor(12, 200);
    tft.print(sdtool_msg);
  }

  if (sdtool_dir == "/")
    drawSkinFooter("LB:Exit MB:Paste RB:Del U/D:Nav L:Move R:Copy OK:Open");
  else
    drawSkinFooter("LB:Back MB:Paste RB:Del U/D:Nav L:Move R:Copy OK:Open");
}

static void sdtool_refresh(const String& msg = "") {
  String err;
  if (!sdtool_loadDir(sdtool_dir, &err)) {
    sdtool_setStatus("ERROR", err);
    sdtool_s = 0; sdtool_scroll = 0;
    sdtool_draw();
    return;
  }

  sdtool_setStatus("READY", msg);
  sdtool_msg = msg;
  sdtool_err = false;

  if (sdtool_s >= sdtool_c) sdtool_s = max(0, sdtool_c - 1);
  if (sdtool_s < sdtool_scroll) sdtool_scroll = sdtool_s;
  if (sdtool_s >= sdtool_scroll + 5) sdtool_scroll = sdtool_s - 4;

  sdtool_draw();
}

static void sdtool_enter() {
  sdtool_beginSD();

  sdtool_cb_has = false;
  sdtool_cb_cut = false;
  sdtool_cb_isDir = false;
  sdtool_cb_path = "";

  sdtool_dir = "/";
  sdtool_s = 0;
  sdtool_scroll = 0;

  if (!sdtool_sd_ok) {
    sdtool_setStatus("ERROR", "SD card not detected. Insert SD card and restart.");
    sdtool_draw();
    tn_logAppEnter(APP_SDTOOL);
    tn_log("SDTOOL", String("Enter, SD=") + (sdtool_sd_ok ? "OK" : "ERROR"));
    return;
  }

  sdtool_refresh("");
  tn_logAppEnter(APP_SDTOOL);
  tn_log("SDTOOL", String("Enter, SD=") + (sdtool_sd_ok ? "OK" : "ERROR"));
}

static void sdtool_copyOrMoveSelected(bool move) {
  if (!sdtool_sd_ok) return;
  if (sdtool_c <= 0) return;

  String name = sdtool_name[sdtool_s];
  if (name == "Back") return;

  String path = sdtool_join(sdtool_dir, name);

  sdtool_cb_has = true;
  sdtool_cb_cut = move;
  sdtool_cb_isDir = sdtool_isDir[sdtool_s];
  sdtool_cb_path = path;

  tn_log("SDTOOL", String(move ? "Move queued: " : "Copy queued: ") + path);
  sdtool_refresh(move ? "Move selected" : "Copied");
}

static void sdtool_pasteAction() {
  if (!sdtool_sd_ok) return;
  if (!sdtool_cb_has) { sdtool_refresh("Clipboard empty"); return; }

  String dest = sdtool_dir;

  sdtool_setStatus("READY", "Pasting...");
  sdtool_draw();

  tn_log("SDTOOL", String("Paste into: ") + dest);

  String err;
  if (!sdtool_pasteInto(dest, &err)) {
    sdtool_setStatus("ERROR", err);
    sdtool_draw();
  } else {
    sdtool_refresh("Pasted");
  }
}

static void sdtool_loop() {
  const int visible = 5;

  if (keys.pressed(MASK_JOY_UP)) {
    if (sdtool_s > 0) sdtool_s--;
    if (sdtool_s < sdtool_scroll) sdtool_scroll = sdtool_s;
    sdtool_draw();
  }

  if (keys.pressed(MASK_JOY_DOWN)) {
    if (sdtool_s < sdtool_c - 1) sdtool_s++;
    if (sdtool_s >= sdtool_scroll + visible) sdtool_scroll = sdtool_s - (visible - 1);
    sdtool_draw();
  }

  if (keys.pressed(MASK_BTN_C)) {
    if (!sdtool_sd_ok) {
      tn_logAppExit(APP_SDTOOL);
      app = APP_MENU;
      menu_draw();
      tn_logAppEnter(APP_MENU);
      return;
    }
    if (sdtool_dir != "/") {
      sdtool_dir = sdtool_parentDir(sdtool_dir);
      sdtool_s = 0; sdtool_scroll = 0;
      sdtool_refresh("");
    } else {
      tn_logAppExit(APP_SDTOOL);
      app = APP_MENU;
      menu_draw();
      tn_logAppEnter(APP_MENU);
      return;
    }
  }

  if (keys.pressed(MASK_BTN_A)) {
    if (!sdtool_sd_ok) return;
    if (sdtool_c <= 0) return;

    String name = sdtool_name[sdtool_s];
    if (name == "Back") return;

    String path = sdtool_join(sdtool_dir, name);

    sdtool_setStatus("READY", "Deleting...");
    sdtool_draw();

    tn_log("SDTOOL", String("Delete: ") + path);

    String err;
    if (!sdtool_rmTree(path, &err)) {
      sdtool_setStatus("ERROR", err);
      sdtool_draw();
    } else {
      sdtool_refresh("Deleted");
    }
  }

  if (keys.pressed(MASK_JOY_LEFT)) {
    sdtool_copyOrMoveSelected(true);
  }

  if (keys.pressed(MASK_JOY_RIGHT)) {
    sdtool_copyOrMoveSelected(false);
  }

  if (keys.pressed(MASK_BTN_B)) {
    sdtool_pasteAction();
  }

  if (keys.pressed(MASK_JOY_OK)) {
    if (!sdtool_sd_ok) return;
    if (sdtool_c <= 0) return;

    String name = sdtool_name[sdtool_s];
    if (sdtool_isDir[sdtool_s]) {
      tn_log("SDTOOL", String("Open dir: ") + name);
      if (name == "Back") sdtool_dir = sdtool_parentDir(sdtool_dir);
      else                sdtool_dir = sdtool_join(sdtool_dir, name);

      sdtool_s = 0; sdtool_scroll = 0;
      sdtool_refresh("");
    } else {
      sdtool_setStatus("READY", "File selected");
      sdtool_draw();
    }
  }
}

void setup() {
  pinMode(BTN_A,     INPUT_PULLUP);
  pinMode(BTN_B,     INPUT_PULLUP);
  pinMode(BTN_C,     INPUT_PULLUP);
  pinMode(JOY_UP,    INPUT_PULLUP);
  pinMode(JOY_DOWN,  INPUT_PULLUP);
  pinMode(JOY_LEFT,  INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);
  pinMode(JOY_OK,    INPUT_PULLUP);

  Serial.begin(TN_SERIAL_BAUD);
  uint32_t serialStart = millis();
  while (!Serial && (millis() - serialStart) < TN_SERIAL_WAIT_MS) { delay(10); }
  tn_serialReady = true;

  tn_log("BOOT", "TechNet boot start");
  tn_logf("BOOT", "Firmware", "TechNet");
  tn_logf("BOOT", "Baud", String(TN_SERIAL_BAUD));

  tft.begin();
  tft.setRotation(3);
  tn_log("DISPLAY", "TFT initialized, rotation=3");

  tn_battInit();
  tn_battPoll(true);

  drawBootLogo();
  tn_log("BOOT", "Boot logo drawn");

  menu_draw();
  tn_logAppEnter(APP_MENU);
}

void loop() {
  readInputs();
  tn_battPoll(false);

  switch (app) {
    case APP_MENU:
      menu_loop();
      break;

    case APP_CHANNEL_MONITOR: {
      static bool entered = false;
      if (!entered) { cm_enter(); entered = true; }
      cm_loop();
      if (app != APP_CHANNEL_MONITOR) entered = false;
      break;
    }

    case APP_SSID_MONITOR: {
      static bool entered = false;
      if (!entered) { mon_enter(); entered = true; }
      mon_loop();
      if (app != APP_SSID_MONITOR) entered = false;
      break;
    }

    case APP_SSID_SCAN: {
      static bool entered = false;
      if (!entered) { ss_enter(); entered = true; }
      ss_loop();
      if (app != APP_SSID_SCAN) entered = false;
      break;
    }

    case APP_BT_MONITOR: {
      static bool entered = false;
      if (!entered) { bt_enter(); entered = true; }
      bt_loop();
      if (app != APP_BT_MONITOR) entered = false;
      break;
    }

    case APP_BT_SCAN: {
      static bool entered = false;
      if (!entered) { bts_enter(); entered = true; }
      bts_loop();
      if (app != APP_BT_SCAN) entered = false;
      break;
    }

    case APP_BADUSB: {
      static bool entered = false;
      if (!entered) { bu_enter(); entered = true; }
      bu_loop();
      if (app != APP_BADUSB) entered = false;
      break;
    }

    case APP_WIOCHAT: {
      static bool entered = false;
      if (!entered) { wc_enter(); entered = true; }
      wc_loop();
      if (app != APP_WIOCHAT) entered = false;
      break;
    }

    case APP_WIODROP: {
      static bool entered = false;
      if (!entered) { wd_enter(); entered = true; }
      wd_loop();
      if (app != APP_WIODROP) entered = false;
      break;
    }

    case APP_SDTOOL: {
      static bool entered = false;
      if (!entered) { sdtool_enter(); entered = true; }
      sdtool_loop();
      if (app != APP_SDTOOL) entered = false;
      break;
    }
  }
}
