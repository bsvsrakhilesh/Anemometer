#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* =============== USER SETTINGS =============== */
char   logFileName[] = "anemometer.CSV";   // 8.3 filename
const  float VFS  = 5.0;                  // full-scale velocity (m s⁻¹)
const  float E0   = 0.0;                  // volts at zero flow
const  float EFS  = 5.0;                  // volts at full scale
const  uint32_t SAMPLE_PERIOD_MS = 1000;
const  uint32_t LOG_PERIOD_MS    = 1000;
/* ============================================ */

const uint8_t PIN_SENSOR = A0;
const float   ADC_REF    = 5.0;
const float   ADC_MAX    = 1023.0;

/* SD-card */
const uint8_t PIN_SD_CS  = 53;

/* OLED */
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* RTC */
RTC_DS3231 rtc;

/* Serial */
const uint32_t SERIAL_BAUD = 115200;
String serialBuffer;

/* Timing helpers */
unsigned long lastSample = 0;
unsigned long lastLog    = 0;

/* ---- helpers ---- */
void formatDate(char *buf, const DateTime &ts) {
  snprintf(buf, 11, "%02d-%02d-%04d", ts.day(), ts.month(), ts.year());   // DD-MM-YYYY
}

/* parse “SET,DD-MM-YYYY,HH:MM:SS”  */
bool parseDateTimeCmd(const String &cmd, DateTime &out) {
  if (!cmd.startsWith("SET,")) return false;

  int first  = cmd.indexOf(',') + 1;
  int second = cmd.indexOf(',', first);
  if (second == -1) return false;

  String dateStr = cmd.substring(first, second);
  String timeStr = cmd.substring(second + 1);

  int d = dateStr.substring(0, 2).toInt();
  int m = dateStr.substring(3, 5).toInt();
  int y = dateStr.substring(6, 10).toInt();

  int hh = timeStr.substring(0, 2).toInt();
  int mm = timeStr.substring(3, 5).toInt();
  int ss = timeStr.substring(6, 8).toInt();

  if (d == 0 || m == 0 || y < 2020) return false;
  out = DateTime(y, m, d, hh, mm, ss);
  return true;
}

/* ------------ SETUP ------------ */
void setup() {
  Wire.begin();
  Serial.begin(SERIAL_BAUD);

  /* OLED */
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { while (true); }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  /* RTC */
  display.clearDisplay(); display.setCursor(0,0); display.print(F("RTC…")); display.display();
  if (!rtc.begin()) {
    display.clearDisplay(); display.setCursor(0,0); display.print(F("RTC NOT FOUND")); display.display();
    while (true);
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  /* SD */
  display.clearDisplay(); display.setCursor(0,0); display.print(F("SD…")); display.display();
  if (!SD.begin(PIN_SD_CS)) {
    display.clearDisplay(); display.setCursor(0,0); display.print(F("SD FAIL")); display.display();
    while (true);
  }
  if (!SD.exists(logFileName)) {
    File f = SD.open(logFileName, FILE_WRITE);
    if (f) { f.println(F("Date,Time,Raw,Volts,Velocity")); f.close(); }
  }

  display.clearDisplay(); display.setCursor(0,0); display.print(F("LOGGER READY")); display.display();
  analogReference(DEFAULT);

  Serial.println(F("Type  SET,DD-MM-YYYY,HH:MM:SS  to change RTC"));
}

/* ------------- LOOP ------------- */
void loop() {
  unsigned long nowMs = millis();

  /* ---- sample & display ---- */
  if (nowMs - lastSample >= SAMPLE_PERIOD_MS) {
    lastSample = nowMs;

    uint16_t raw = analogRead(PIN_SENSOR);
    float    V   = raw * ADC_REF / ADC_MAX;
    float    vel = max(0.0, (V - E0) / (EFS - E0)) * VFS;

    DateTime ts = rtc.now();
    char dateStr[11]; formatDate(dateStr, ts);
    char timeStr[9];  sprintf(timeStr, "%02d:%02d:%02d", ts.hour(), ts.minute(), ts.second());

    display.clearDisplay();
    display.setCursor(0,0);  display.print(dateStr);
    display.setCursor(0,9);  display.print(timeStr);
    display.setCursor(0,20); display.print(F("Vel : ")); display.print(vel,3); display.print(F(" m/s"));
    display.display();
  }

  /* ---- SD logging ---- */
  if (nowMs - lastLog >= LOG_PERIOD_MS) {
    lastLog = nowMs;

    uint16_t raw = analogRead(PIN_SENSOR);
    float    V   = raw * ADC_REF / ADC_MAX;
    float    vel = max(0.0, (V - E0) / (EFS - E0)) * VFS;

    DateTime ts = rtc.now();
    char dateStr[11]; formatDate(dateStr, ts);
    char timeStr[9];  sprintf(timeStr, "%02d:%02d:%02d", ts.hour(), ts.minute(), ts.second());

    File f = SD.open(logFileName, FILE_WRITE);
    if (f) {
      f.print(dateStr); f.print(','); f.print(timeStr); f.print(',');
      f.print(raw); f.print(','); f.print(V,3); f.print(','); f.println(vel,3);
      f.close();
    }
  }

  /* ---- Serial date/time update ---- */
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length()) {
        DateTime newDT;
        if (parseDateTimeCmd(serialBuffer, newDT)) {
          rtc.adjust(newDT);
          Serial.println(F("RTC updated."));
        } else {
          Serial.println(F("Invalid command. Use: SET,DD-MM-YYYY,HH:MM:SS"));
        }
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}
