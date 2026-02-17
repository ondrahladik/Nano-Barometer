#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <U8glib.h>
#include <EEPROM.h>

#define BUTTON_PIN 3
#define EEPROM_ADDR 0

#define LONG_PRESS_TIME 500
#define REPEAT_INTERVAL 200

U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_NONE);
Adafruit_BMP280 bmp;

volatile bool buttonPressed = false;
volatile bool buttonReleased = false;
volatile unsigned long pressStartTime = 0;

int altitude = 230;

unsigned long lastRepeatTime = 0;
bool longMode = false;

void buttonISR() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    buttonPressed = true;
    buttonReleased = false;
    pressStartTime = millis();
    longMode = false;
  } else {
    buttonPressed = false;
    buttonReleased = true;
  }
}

void setup() {

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  EEPROM.get(EEPROM_ADDR, altitude);
  if (altitude < 0 || altitude > 5000) {
    altitude = 230;
    EEPROM.put(EEPROM_ADDR, altitude);
  }

  if (!bmp.begin(0x76)) {
    if (!bmp.begin(0x77)) {
      while (1);
    }
  }

  bmp.setSampling(
    Adafruit_BMP280::MODE_NORMAL,
    Adafruit_BMP280::SAMPLING_X2,
    Adafruit_BMP280::SAMPLING_X16,
    Adafruit_BMP280::FILTER_X16,
    Adafruit_BMP280::STANDBY_MS_500
  );

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, CHANGE);
}

void loop() {

  handleButton();

  float temperature = bmp.readTemperature();
  float absolutePressure = bmp.readPressure() / 100.0;

  float relativePressure = absolutePressure /
    pow(1.0 - (0.0065 * altitude) / (temperature + 273.15), 5.255);

  char buffer[20];
  char altBuffer[10];

  u8g.firstPage();
  do {
    
    u8g.setFont(u8g_font_6x12);

    dtostrf(temperature, 5, 2, buffer);
    strcat(buffer, " C");
    u8g.drawStr(0, 12, buffer);

    sprintf(altBuffer, "%d m", altitude);
    int width = u8g.getStrWidth(altBuffer);
    u8g.drawStr(128 - width, 12, altBuffer);

    u8g.drawLine(0, 14, 127, 14);

    // ===== ABS =====
    u8g.setFont(u8g_font_6x12);
    u8g.drawStr(0, 30, "ABS:");

    u8g.setFont(u8g_font_fub17);
    dtostrf(absolutePressure, 6, 2, buffer);
    u8g.drawStr(35, 38, buffer);

    // ===== REL =====
    u8g.setFont(u8g_font_6x12);
    u8g.drawStr(0, 54, "REL:");

    u8g.setFont(u8g_font_fub17);
    dtostrf(relativePressure, 6, 2, buffer);
    u8g.drawStr(35, 62, buffer);

  } while (u8g.nextPage());

  delay(80);
}

void handleButton() {

  if (buttonPressed) {

    unsigned long heldTime = millis() - pressStartTime;

    if (!longMode && heldTime > LONG_PRESS_TIME) {
      longMode = true;
      lastRepeatTime = millis();
    }

    if (longMode && millis() - lastRepeatTime > REPEAT_INTERVAL) {

      altitude += 100;
      if (altitude >= 5000) altitude = 0;

      EEPROM.put(EEPROM_ADDR, altitude);
      lastRepeatTime = millis();
    }
  }

  if (buttonReleased) {

    buttonReleased = false;

    if (!longMode) {

      altitude += 5;
      if (altitude >= 5000) altitude = 0;

      EEPROM.put(EEPROM_ADDR, altitude);
    }
  }
}
