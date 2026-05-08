#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SD.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// === OLED ===
#define OLED_CS   16
#define OLED_DC   5
#define OLED_RES  4
Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RES, OLED_CS);

// === SD ===
#define SD_CS 15

// === Кнопки ===
#define BTN_DOWN  0
#define BTN_UP    2
#define BTN_LEFT  3
#define BTN_RIGHT 1

// === WiFi ===
bool wifiConnected = false;
String wifiIP = "";

// === Меню (легко улучшать) ===
#define MENU_ITEMS 6
const char* menuItems[MENU_ITEMS] = {"WiFi Scan", "Browser", "Files", "Settings", "nan1", "nan2"};
int menuSel = 0;      // выбранный пункт
int menuTop = 0;      // первый видимый пункт
#define MENU_VISIBLE 3  // сколько пунктов видно на экране

// ========================
// ЗНАЧОК WIFI (три кружочка, компактно)
// ========================
void drawWifiIcon() {
  // Позиция: правый верхний угол, отступ 2px от краёв
  int x0 = 120;  // центр последнего кружка
  int y0 = 4;    // центр кружков по Y
  int r = 2;     // радиус кружка
  int gap = 5;   // расстояние между центрами
  
  if (wifiConnected) {
    int rssi = WiFi.RSSI();
    int bars;
    if (rssi > -50) bars = 3;
    else if (rssi > -70) bars = 2;
    else bars = 1;
    
    for (int i = 0; i < 3; i++) {
      int cx = x0 - (2 - i) * gap;
      if (i < bars) {
        display.fillCircle(cx, y0, r, SSD1306_WHITE);
      } else {
        display.drawCircle(cx, y0, r, SSD1306_WHITE);
      }
    }
  } else {
    // Три пустых кружка
    for (int i = 0; i < 3; i++) {
      int cx = x0 - (2 - i) * gap;
      display.drawCircle(cx, y0, r, SSD1306_WHITE);
    }
  }
}

// ========================
// КЛАВИАТУРА (залипание для UP/DOWN, без залипания для RIGHT)
// ========================
const char charset[] = 
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789"
  "!@#$%^&*()-_=+.,:/ ";
const int charsetLen = sizeof(charset) - 1;

String keyboard(String title, int maxLen) {
  String result = "";
  int charIndex = 0;
  int holdDelay = 0;
  bool rightWasPressed = false;
  
  // Ждём отпускания RIGHT перед началом
  while (!digitalRead(BTN_RIGHT)) delay(10);
  delay(200);
  
  while (true) {
    display.clearDisplay();
    drawWifiIcon();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    display.setCursor(0, 0);
    display.print(title);
    
    display.setCursor(0, 12);
    display.print(result);
    display.print("_");
    
    char c = charset[charIndex];
    if (c == ' ') c = '_';
    int cx = 56;
    int cy = 42;
    
    display.fillRect(cx - 8, cy - 2, 16, 16, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(cx - 2, cy);
    display.print(c);
    display.setTextColor(SSD1306_WHITE);
    
    display.display();
    
    bool up    = !digitalRead(BTN_UP);
    bool down  = !digitalRead(BTN_DOWN);
    bool right = !digitalRead(BTN_RIGHT);
    bool left  = !digitalRead(BTN_LEFT);
    
    // UP/DOWN — с залипанием для быстрой прокрутки
    if (up) {
      holdDelay++;
      if (holdDelay == 1 || holdDelay > 10) {
        charIndex = (charIndex - 1 + charsetLen) % charsetLen;
        if (holdDelay > 10) delay(30);  // быстро
        else delay(150);                // первый раз медленно
      }
    }
    else if (down) {
      holdDelay++;
      if (holdDelay == 1 || holdDelay > 10) {
        charIndex = (charIndex + 1) % charsetLen;
        if (holdDelay > 10) delay(30);
        else delay(150);
      }
    } else {
      holdDelay = 0;
    }
    
    // RIGHT — строго одно нажатие, ждём отпускания
    if (right && !rightWasPressed && result.length() < maxLen) {
      result += charset[charIndex];
      rightWasPressed = true;
    }
    if (!right) {
      rightWasPressed = false;
    }
    
    // LEFT — выход
    if (left) {
      delay(200);
      while (!digitalRead(BTN_LEFT)) delay(10);
      return result;
    }
    
    delay(20);
  }
}

// ========================
// WiFi СКАНЕР
// ========================
struct WiFiNet {
  String ssid;
  int rssi;
  bool open;
};

WiFiNet networks[50];
int netCount = 0;
int netIndex = 0;

void wifiScanner() {
  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("Scanning...");
  display.display();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  netCount = WiFi.scanNetworks();
  if (netCount > 50) netCount = 50;
  
  for (int i = 0; i < netCount; i++) {
    networks[i].ssid = WiFi.SSID(i);
    networks[i].rssi = WiFi.RSSI(i);
    networks[i].open = (WiFi.encryptionType(i) == ENC_TYPE_NONE);
  }
  
  // Сортировка
  for (int i = 0; i < netCount - 1; i++) {
    for (int j = i + 1; j < netCount; j++) {
      if (networks[j].rssi > networks[i].rssi) {
        WiFiNet t = networks[i];
        networks[i] = networks[j];
        networks[j] = t;
      }
    }
  }
  
  netIndex = 0;
  showNetworks();
  
  while (true) {
    bool up    = !digitalRead(BTN_UP);
    bool down  = !digitalRead(BTN_DOWN);
    bool right = !digitalRead(BTN_RIGHT);
    bool left  = !digitalRead(BTN_LEFT);
    
    if (up && netIndex > 0) {
      netIndex--;
      showNetworks();
      delay(200);
      while (!digitalRead(BTN_UP)) delay(10);
    }
    if (down && netIndex < netCount - 1) {
      netIndex++;
      showNetworks();
      delay(200);
      while (!digitalRead(BTN_DOWN)) delay(10);
    }
    
    if (right) {
      delay(200);
      while (!digitalRead(BTN_RIGHT)) delay(10);
      
      if (networks[netIndex].open) {
        connectWiFi(networks[netIndex].ssid, "");
      } else {
        String pass = keyboard("Password", 32);
        if (pass.length() > 0) {
          connectWiFi(networks[netIndex].ssid, pass);
        }
      }
      return;
    }
    
    if (left) {
      delay(200);
      while (!digitalRead(BTN_LEFT)) delay(10);
      return;
    }
    delay(30);
  }
}

void showNetworks() {
  display.clearDisplay();
  drawWifiIcon();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.print("Net: ");
  display.print(netCount);
  display.drawLine(0, 9, 112, 9, SSD1306_WHITE);
  
  int visible = 5;
  int start = netIndex - 2;
  if (start < 0) start = 0;
  if (start > netCount - visible) start = netCount - visible;
  if (start < 0) start = 0;
  
  for (int i = 0; i < visible && (start + i) < netCount; i++) {
    int idx = start + i;
    int y = 12 + i * 10;
    
    String name = networks[idx].ssid;
    if (name.length() > 14) name = name.substring(0, 13) + "~";
    
    display.setCursor(0, y);
    if (idx == netIndex) display.print(">");
    else display.print(" ");
    display.print(networks[idx].open ? "O" : "L");
    display.print(name);
    
    String rssiStr = String(networks[idx].rssi);
    display.setCursor(112 - rssiStr.length() * 6, y);
    display.print(rssiStr);
  }
  
  display.display();
}

void connectWiFi(String ssid, String pass) {
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("Connecting...");
  display.setCursor(0, 30);
  display.println(ssid);
  display.display();
  
  WiFi.begin(ssid.c_str(), pass.c_str());
  
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    display.print(".");
    display.display();
    tries++;
  }
  
  display.clearDisplay();
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiIP = WiFi.localIP().toString();
    
    display.setCursor(0, 10);
    display.println("Connected!");
    display.setCursor(0, 30);
    display.println(wifiIP);
    
    File f = SD.open("/wifi.txt", FILE_WRITE);
    if (f) { f.print(ssid); f.print(" | "); f.println(pass); f.close(); }
  } else {
    wifiConnected = false;
    display.setCursor(0, 10);
    display.println("Failed");
  }
  display.display();
  delay(2000);
}

// ========================
// БРАУЗЕР
// ========================
String browserResult = "";
int browserScroll = 0;

void browser() {
  // Для теста — сразу рабочий URL
  String input = "http://example.com";
  
  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("Loading...");
  display.display();
  
  browserResult = httpGet(input);
  browserScroll = 0;
  showBrowser();
  
  while (true) {
    bool up   = !digitalRead(BTN_UP);
    bool down = !digitalRead(BTN_DOWN);
    bool left = !digitalRead(BTN_LEFT);
    
    if (up && browserScroll > 0) {
      browserScroll--;
      showBrowser();
      delay(150);
      while (!digitalRead(BTN_UP)) delay(10);
    }
    if (down && browserScroll < 200) {
      browserScroll++;
      showBrowser();
      delay(150);
      while (!digitalRead(BTN_DOWN)) delay(10);
    }
    if (left) {
      delay(200);
      while (!digitalRead(BTN_LEFT)) delay(10);
      return;
    }
    delay(30);
  }
}

void showBrowser() {
  display.clearDisplay();
  drawWifiIcon();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  int charPerLine = 20;
  int pos = 0;
  int lineNum = 0;
  int startLine = browserScroll;
  
  while (pos < browserResult.length() && lineNum < startLine + 8) {
    String line = "";
    int len = 0;
    
    while (pos < browserResult.length() && len < charPerLine) {
      char c = browserResult[pos];
      if (c == '\n') { pos++; break; }
      line += c;
      pos++;
      len++;
    }
    
    if (lineNum >= startLine) {
      display.setCursor(0, (lineNum - startLine) * 8);
      display.println(line);
    }
    lineNum++;
  }
  
  display.display();
}

String httpGet(String url) {
  if (WiFi.status() != WL_CONNECTED) return "No WiFi connection";

  // Парсим URL
  String host = "";
  String path = "/";
  
  if (url.startsWith("http://")) url = url.substring(7);
  if (url.startsWith("https://")) url = url.substring(8);
  
  int slash = url.indexOf('/');
  if (slash > 0) {
    host = url.substring(0, slash);
    path = url.substring(slash);
  } else {
    host = url;
    path = "/";
  }

  // DNS
  IPAddress ip;
  if (!WiFi.hostByName(host.c_str(), ip)) {
    return "DNS failed";
  }

  // TCP Connect с повтором
  WiFiClient client;
  int tries = 0;
  int conn = 0;
  
  while (tries < 3 && !conn) {
    conn = client.connect(ip, 80);
    tries++;
    if (!conn) delay(200);
  }
  
  if (!conn) return "TCP connect failed after 3 tries";

  // HTTP GET
  client.print("GET " + path + " HTTP/1.0\r\n");
  client.print("Host: " + host + "\r\n");
  client.print("User-Agent: Mozilla/5.0\r\n");
  client.print("Connection: close\r\n");
  client.print("\r\n");

  // Ждём ответ
  unsigned long timeout = millis() + 10000;
  while (!client.available() && millis() < timeout) delay(10);

  // Читаем, пропуская заголовки
  String result = "";
  bool headersDone = false;
  
  while ((client.available() || client.connected()) && result.length() < 3000) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      
      if (!headersDone) {
        if (line == "\r" || line == "\n" || line.length() <= 1) {
          headersDone = true;
        }
      } else {
        result += line;
      }
      
      timeout = millis() + 5000;
    }
  }
  client.stop();

  String clean = stripHTML(result);
  if (clean.length() > 2000) clean = clean.substring(0, 2000);
  
  return clean;
}

String stripHTML(String html) {
  String result = "";
  bool inTag = false;
  bool inScript = false;
  
  for (int i = 0; i < html.length(); i++) {
    char c = html[i];
    
    if (c == '<') {
      if (html.substring(i, i + 7) == "<script" || html.substring(i, i + 6) == "<style") inScript = true;
      inTag = true;
    }
    else if (c == '>') {
      if (inScript && (html.substring(i - 7, i + 1) == "</script" || html.substring(i - 6, i + 1) == "</style")) inScript = false;
      inTag = false;
    }
    else if (!inTag && !inScript) {
      if (c == '&') {
        if (html.substring(i, i + 5) == "&amp;") { result += '&'; i += 4; }
        else if (html.substring(i, i + 4) == "&lt;") { result += '<'; i += 3; }
        else if (html.substring(i, i + 4) == "&gt;") { result += '>'; i += 3; }
        else if (html.substring(i, i + 6) == "&nbsp;") { result += ' '; i += 5; }
        else if (html.substring(i, i + 6) == "&quot;") { result += '"'; i += 5; }
        else result += c;
      } else result += c;
    }
  }
  
  String clean = "";
  bool lastWasSpace = false;
  for (int i = 0; i < result.length(); i++) {
    char c = result[i];
    if (c == '\n' || c == '\r') {
      if (!lastWasSpace) { clean += '\n'; lastWasSpace = true; }
    } else if (c == ' ' || c == '\t') {
      if (!lastWasSpace) { clean += ' '; lastWasSpace = true; }
    } else {
      clean += c;
      lastWasSpace = false;
    }
  }
  
  return clean;
}

String urlEncode(String str) {
  String encoded = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", c);
      encoded += buf;
    }
  }
  return encoded;
}

// ========================
// SETTINGS
// ========================
void showSettings() {
  while (true) {
    display.clearDisplay();
    drawWifiIcon();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    display.setCursor(0, 0);
    display.println("=== SETTINGS ===");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    
    if (wifiConnected) {
      display.setCursor(0, 16);
      display.print("WiFi: ");
      display.print(WiFi.SSID());
      display.setCursor(0, 28);
      display.print("IP: ");
      display.print(wifiIP);
      display.setCursor(0, 40);
      display.print("RSSI: ");
      display.print(WiFi.RSSI());
      display.print(" dBm");
    } else {
      display.setCursor(0, 16);
      display.print("WiFi: Disconnected");
    }
    
    display.setCursor(0, 56);
    display.print("LEFT to exit");
    display.display();
    
    if (!digitalRead(BTN_LEFT)) {
      delay(200);
      while (!digitalRead(BTN_LEFT)) delay(10);
      return;
    }
    delay(30);
  }
}

// ========================
// МЕНЮ (плавная прокрутка, легко расширять)
// ========================
void showMenu() {
  display.clearDisplay();
  drawWifiIcon();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Заголовок
  display.setCursor(30, 0);
  display.print("MENU");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  
  // Плавная прокрутка: вычисляем topIndex
  if (menuSel < menuTop) menuTop = menuSel;
  if (menuSel >= menuTop + MENU_VISIBLE) menuTop = menuSel - MENU_VISIBLE + 1;
  if (menuTop > MENU_ITEMS - MENU_VISIBLE) menuTop = MENU_ITEMS - MENU_VISIBLE;
  if (menuTop < 0) menuTop = 0;
  
  // Рисуем видимые пункты
  for (int i = 0; i < MENU_VISIBLE; i++) {
    int idx = menuTop + i;
    if (idx >= MENU_ITEMS) break;
    
    int y = 16 + i * 14;  // 14px на пункт
    int textW = strlen(menuItems[idx]) * 6;
    int x = (128 - textW) / 2;
    
    if (idx == menuSel) {
      // Выделенный пункт
      display.fillRect(10, y - 1, 108, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(x, y);
      display.print(menuItems[idx]);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(x, y);
      display.print(menuItems[idx]);
    }
  }
  
  // Индикатор прокрутки если есть пункты сверху/снизу
  if (menuTop > 0) {
    display.fillTriangle(64, 55,// up
                         61, 58,
                         67, 58,
                         SSD1306_WHITE);

  }
  if (menuTop + MENU_VISIBLE < MENU_ITEMS) {
    display.fillTriangle(64, 63,// down
                         61, 60,
                         67, 60,
                         SSD1306_WHITE);
  }
  
  display.display();
}

// ========================
void setup() {
  Serial.begin(115200);
  display.begin(SSD1306_SWITCHCAPVCC);
  SD.begin(SD_CS);
  
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  
  WiFi.mode(WIFI_STA);
  
  menuSel = 0;
  menuTop = 0;
  showMenu();
}

void loop() {
  static bool prevUp = 0, prevDown = 0, prevRight = 0;
  
  bool up    = !digitalRead(BTN_UP);
  bool down  = !digitalRead(BTN_DOWN);
  bool right = !digitalRead(BTN_RIGHT);
  
  if (up && !prevUp) {
    if (menuSel > 0) {
      menuSel--;
      showMenu();
    }
  }
  if (down && !prevDown) {
    if (menuSel < MENU_ITEMS - 1) {
      menuSel++;
      showMenu();
    }
  }
  
  if (right && !prevRight) {
    if (menuSel == 0) {
      wifiScanner();
      showMenu();
    }
    if (menuSel == 1) {
      browser();
      showMenu();
    }
    if (menuSel == 3) {
      showSettings();
      showMenu();
    }
  }
  
  prevUp = up;
  prevDown = down;
  prevRight = right;
  
  delay(30);
}
