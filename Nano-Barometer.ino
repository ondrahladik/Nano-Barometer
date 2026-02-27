// ===================
// NANO BAROMETER - Arduino Nano + BMP280 + SH1106 OLED
// https://github.com/ondrahladik/Nano-Barometer
// ===================

#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <U8g2lib.h>
#include <EEPROM.h>

// ===================
// CONFIGURATION
// ===================

#define BUTTON_PIN 3
#define EEPROM_ADDR 0

#define LONG_PRESS_TIME      500UL
#define REPEAT_INTERVAL      200UL
#define MEASUREMENT_INTERVAL 500UL
#define DISPLAY_INTERVAL     100UL
#define WELCOME_DURATION     3000UL

#define ALTITUDE_MIN     0
#define ALTITUDE_MAX     5000
#define ALTITUDE_DEFAULT 230

// Calibration offsets 
#define TEMPERATURE_OFFSET -2.0f
#define PRESSURE_OFFSET    0.0f

#define FILTER_SIZE 5

// ===================
// HARDWARE INSTANCES
// ===================

U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);
Adafruit_BMP280 bmp;

// ===================
// STATE STRUCTURES
// ===================

static volatile bool buttonPressed = false;
static volatile bool buttonReleased = false;
static volatile unsigned long pressStartTime = 0;

static struct {
  unsigned long lastRepeatTime;
  bool longMode;
} buttonState = {0, false};

static struct {
  unsigned long lastMeasurement;
  unsigned long lastDisplay;
} timing = {0, 0};

static struct {
  float temperatureBuffer[FILTER_SIZE];
  float pressureBuffer[FILTER_SIZE];
  uint8_t bufferIndex;
  bool bufferFilled;
  float temperature;
  float absolutePressure;
  float relativePressure;
  bool sensorOk;
} sensorData = {{0}, {0}, 0, false, 0.0f, 0.0f, 0.0f, false};

static int16_t altitude = ALTITUDE_DEFAULT;

// ===================
// INTERRUPT SERVICE ROUTINE
// ===================

void buttonISR() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    buttonPressed = true;
    buttonReleased = false;
    pressStartTime = millis();
    buttonState.longMode = false;
  } else {
    buttonPressed = false;
    buttonReleased = true;
  }
}

// ===================
// SENSOR FUNCTIONS
// ===================

static bool initSensor() {
  if (bmp.begin(0x76)) {
    return true;
  }
  if (bmp.begin(0x77)) {
    return true;
  }
  return false;
}

static void configureSensor() {
  bmp.setSampling(
    Adafruit_BMP280::MODE_NORMAL,
    Adafruit_BMP280::SAMPLING_X2,
    Adafruit_BMP280::SAMPLING_X16,
    Adafruit_BMP280::FILTER_X16,
    Adafruit_BMP280::STANDBY_MS_500
  );
}

static float calculateAverage(const float* buffer, uint8_t count) {
  float sum = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    sum += buffer[i];
  }
  return sum / count;
}

static bool readSensor() {
  float rawTemp = bmp.readTemperature();
  float rawPressure = bmp.readPressure();
  
  if (isnan(rawTemp) || isnan(rawPressure) || 
      rawPressure < 30000.0f || rawPressure > 120000.0f) {
    sensorData.sensorOk = false;
    return false;
  }
  
  sensorData.sensorOk = true;
  
  // Apply calibration offsets
  rawTemp += TEMPERATURE_OFFSET;
  rawPressure = (rawPressure / 100.0f) + PRESSURE_OFFSET;
  
  sensorData.temperatureBuffer[sensorData.bufferIndex] = rawTemp;
  sensorData.pressureBuffer[sensorData.bufferIndex] = rawPressure;
  
  // Advance buffer index
  sensorData.bufferIndex++;
  if (sensorData.bufferIndex >= FILTER_SIZE) {
    sensorData.bufferIndex = 0;
    sensorData.bufferFilled = true;
  }
  
  // Calculate filtered values
  uint8_t sampleCount = sensorData.bufferFilled ? FILTER_SIZE : sensorData.bufferIndex;
  if (sampleCount == 0) sampleCount = 1;
  
  sensorData.temperature = calculateAverage(sensorData.temperatureBuffer, sampleCount);
  sensorData.absolutePressure = calculateAverage(sensorData.pressureBuffer, sampleCount);
  
  // Calculate sea-level pressure (relative pressure)
  float tempKelvin = sensorData.temperature + 273.15f;
  float pressureRatio = 1.0f - (0.0065f * altitude) / tempKelvin;
  
  if (pressureRatio > 0.0f) {
    sensorData.relativePressure = sensorData.absolutePressure / pow(pressureRatio, 5.255f);
  } else {
    sensorData.relativePressure = sensorData.absolutePressure;
  }
  
  return true;
}

// ===================
// EEPROM FUNCTIONS
// ===================

static void loadAltitude() {
  EEPROM.get(EEPROM_ADDR, altitude);
  if (altitude < ALTITUDE_MIN || altitude > ALTITUDE_MAX) {
    altitude = ALTITUDE_DEFAULT;
    EEPROM.put(EEPROM_ADDR, altitude);
  }
}

static void saveAltitude() {
  EEPROM.put(EEPROM_ADDR, altitude);
}

// ===================
// BUTTON HANDLING
// ===================

static void handleButton() {
  if (buttonPressed) {
    unsigned long currentTime = millis();
    unsigned long heldTime = currentTime - pressStartTime;
    
    if (!buttonState.longMode && heldTime > LONG_PRESS_TIME) {
      buttonState.longMode = true;
      buttonState.lastRepeatTime = currentTime;
    }
    
    if (buttonState.longMode && (currentTime - buttonState.lastRepeatTime) > REPEAT_INTERVAL) {
      altitude += 100;
      if (altitude >= ALTITUDE_MAX) {
        altitude = ALTITUDE_MIN;
      }
      saveAltitude();
      buttonState.lastRepeatTime = currentTime;
    }
  }

  if (buttonReleased) {
    buttonReleased = false;
    
    if (!buttonState.longMode) {
      altitude += 5;
      if (altitude >= ALTITUDE_MAX) {
        altitude = ALTITUDE_MIN;
      }
      saveAltitude();
    }
  }
}

// ===================
// DISPLAY FUNCTIONS
// ===================

static void welcomePage() {
  unsigned long startTime = millis();
  
  while ((millis() - startTime) < WELCOME_DURATION) {
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_helvB18_tr);
      int16_t nanoWidth = u8g2.getStrWidth("NANO");
      u8g2.drawStr((128 - nanoWidth) / 2, 30, "NANO");
      
      u8g2.setFont(u8g2_font_helvR10_tr);
      int16_t baroWidth = u8g2.getStrWidth("BAROMETER");
      u8g2.drawStr((128 - baroWidth) / 2, 50, "BAROMETER");
      
    } while (u8g2.nextPage());
  }
}

static void drawErrorScreen() {
  u8g2.setFont(u8g2_font_mozart_nbp_tr);
  u8g2.drawStr(28, 30, "SENSOR ERROR");
  u8g2.drawStr(28, 46, "Check wiring");
}

static void drawMainScreen() {
  char buffer[12];
  
  // Header line 
  u8g2.setFont(u8g2_font_mozart_nbp_tr);
  
  // Temperature
  dtostrf(sensorData.temperature, 4, 1, buffer);
  u8g2.drawStr(0, 10, buffer);
  uint8_t textWidth = u8g2.getStrWidth(buffer);
  u8g2.drawDisc(textWidth + 3, 4, 1);  
  u8g2.drawStr(textWidth + 6, 10, "C");

  // Altitude 
  itoa(altitude, buffer, 10);
  strcat_P(buffer, PSTR(" m"));
  int16_t altWidth = u8g2.getStrWidth(buffer);
  u8g2.drawStr(128 - altWidth, 10, buffer);
  
  u8g2.drawHLine(0, 13, 128);
  
  // Absolute pressure
  u8g2.setFont(u8g2_font_mozart_nbp_tr);
  u8g2.drawStr(0, 28, "ABS");
  u8g2.setFont(u8g2_font_helvB18_tn);
  dtostrf(sensorData.absolutePressure, 8, 2, buffer);
  u8g2.drawStr(38, 38, buffer);
 
  // Relative pressure (sea level)
  u8g2.setFont(u8g2_font_mozart_nbp_tr);
  u8g2.drawStr(0, 54, "REL");
  u8g2.setFont(u8g2_font_helvB18_tn);
  dtostrf(sensorData.relativePressure, 7, 2, buffer);
  u8g2.drawStr(38, 62, buffer);
}

static void updateDisplay() {
  u8g2.firstPage();
  do {
    if (sensorData.sensorOk) {
      drawMainScreen();
    } else {
      drawErrorScreen();
    }
  } while (u8g2.nextPage());
}

// ===================
// SETUP
// ===================

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  loadAltitude();
  
  u8g2.begin();
  
  sensorData.sensorOk = initSensor();
  if (sensorData.sensorOk) {
    configureSensor();
  }
  
  welcomePage();
  
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, CHANGE);
  
  timing.lastMeasurement = millis();
  timing.lastDisplay = millis();
  
  if (sensorData.sensorOk) {
    readSensor();
  }
}

// ===================
// MAIN LOOP 
// ===================

void loop() {
  unsigned long currentTime = millis();
  
  handleButton();
  
  if ((currentTime - timing.lastMeasurement) >= MEASUREMENT_INTERVAL) {
    timing.lastMeasurement = currentTime;
    
    if (!sensorData.sensorOk) {
      sensorData.sensorOk = initSensor();
      if (sensorData.sensorOk) {
        configureSensor();
      }
    }
    
    if (sensorData.sensorOk) {
      readSensor();
    }
  }
  
  if ((currentTime - timing.lastDisplay) >= DISPLAY_INTERVAL) {
    timing.lastDisplay = currentTime;
    updateDisplay();
  }
}
