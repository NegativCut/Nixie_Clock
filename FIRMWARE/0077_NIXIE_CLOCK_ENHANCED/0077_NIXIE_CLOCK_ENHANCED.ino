#include "exixe.h"
#include "RTClib.h"
#include <EEPROM.h>
// Watchdog disabled - was causing startup issues

RTC_DS3231 rtc;

// EEPROM addresses
#define EEPROM_BRIGHTNESS_ADDR 0
#define EEPROM_LED_RED_ADDR 1
#define EEPROM_LED_GREEN_ADDR 2
#define EEPROM_LED_BLUE_ADDR 3
#define EEPROM_MAGIC_ADDR 4
#define EEPROM_MAGIC_VALUE 0xA5

// Define parameters for nixies
const int cs1 = 10;
const int cs2 = 9;
const int cs3 = 8;
const int cs4 = 7;
const int cs5 = 6;
const int cs6 = 5;
uint8_t brightness = 55; // Default, loaded from EEPROM (range 0-127, exixe library cap)
const uint8_t overdrive = 0;

// Digit position constants for 6-tube layout (positions 0-5, left to right)
const int NUM_TUBES = 6;
const int PAIR_FIRST_END = 2;    // hours/day occupy digits [0..2)
const int PAIR_SECOND_END = 4;   // minutes/month occupy digits [2..4)
                                 // seconds/year occupy digits [4..6)
const int VALUE_DIGIT_START = 3; // brightness/LED 3-digit value occupies [3..6)

// Nixie tube array
exixe tubes[NUM_TUBES] = {exixe(cs1), exixe(cs2), exixe(cs3), exixe(cs4), exixe(cs5), exixe(cs6)};

// RGB LED values (loaded from EEPROM or defaults; range 0-127, exixe library cap)
uint8_t led_red = 127;
uint8_t led_green = 000;
uint8_t led_blue = 000;

// Rotary encoder pins
#define REA_PIN 2    // D2
#define REB_PIN 3    // D3
#define RESW_PIN A0  // Switch on A0

// Timing constants (all unsigned long for consistency with millis()-based arithmetic)
const unsigned long DATE_DISPLAY_INTERVAL = 300000; // Time display duration before showing date
const unsigned long DATE_SHOW_DURATION = 5000;      // How long to show date
const unsigned long FRAME_INTERVAL = EXIXE_ANIMATION_FRAME_DURATION_MS; // 33ms per frame
const unsigned long LED_REFRESH_INTERVAL = 1000;    // Refresh LEDs every 1s
const unsigned long ENCODER_POLL_INTERVAL = 5;      // Poll encoder every 5ms (reduced from 2ms)
const unsigned long SHORT_PRESS_THRESHOLD = 200;    // Short press under 200ms
const unsigned long LONG_PRESS_THRESHOLD = 1000;    // Long press over 1000ms
const unsigned long BLINK_INTERVAL = 500;           // Blink rate for set mode
const unsigned int CROSSFADE_DURATION = 10;         // Frames; passed to exixe library as unsigned int
const unsigned long SET_MODE_TIMEOUT = 300000;      // 5 minutes in set mode before auto-exit
const unsigned long ANTI_POISON_INTERVAL = 21600000; // 6 hours in milliseconds
const unsigned long ANTI_POISON_CYCLE_DURATION = 100; // Duration per digit in ms

unsigned long previousMillis = 0;
unsigned long lastFrameTime = 0;
unsigned long lastLedRefresh = 0;
unsigned long lastAntiPoisonCycle = 0;
unsigned long setModeEntryTime = 0; // Set on entry to a set mode; gated by currentMode != NORMAL

// Track digits and mode
int8_t prev_digits[NUM_TUBES] = { -1, -1, -1, -1, -1, -1}; // -1 sentinel = "force redraw"
uint8_t target_digits[NUM_TUBES] = {0, 0, 0, 0, 0, 0};      // values 0-9 only
bool isDateMode = false;
bool isAntiPoisonRunning = false;
uint8_t antiPoisonDigit = 0;
unsigned long antiPoisonDigitTime = 0;

// Set mode variables - now using enum for LED color mode
enum SetMode { 
  NORMAL, 
  SET_HOURS, 
  SET_MINUTES, 
  SET_SECONDS, 
  SET_DAY, 
  SET_MONTH, 
  SET_YEAR, 
  SET_BRIGHTNESS,
  SET_LED_RED,
  SET_LED_GREEN,
  SET_LED_BLUE
};

SetMode currentMode = NORMAL;

// DateTime struct for better memory organization
struct DateTimeSet {
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t day;
  uint8_t month;
  uint8_t year; // Last 2 digits
} setValues = {0, 0, 0, 1, 1, 0};

unsigned long switchPressTime = 0;
bool switchPressed = false;
unsigned long lastBlinkTime = 0;
bool blinkState = true;

// Encoder variables with improved state table
int encoderPos = 0;
uint8_t encoderState = 0; // 4-bit state tracker
int8_t encoderSubStep = 0; // Accumulates quadrature sub-steps (4 per detent)
unsigned long lastEncoderRead = 0;

// Encoder state lookup table for cleaner rotation detection
const int8_t ENCODER_TABLE[] = {
  0, -1, 1, 0,  // 00 -> 00, 01, 10, 11
  1, 0, 0, -1,  // 01 -> 00, 01, 10, 11
  -1, 0, 0, 1,  // 10 -> 00, 01, 10, 11
  0, 1, -1, 0   // 11 -> 00, 01, 10, 11
};

void setup() {
  if (!rtc.begin()) {
    // RTC failed - flash all tubes and halt
    flashErrorPattern();
    while (1); // Halt
  }

  // Check if RTC needs to be set to compile time
  // Only set if: battery died OR time is wildly wrong (before year 2020)
  DateTime now = rtc.now();
  bool batteryWasDead = rtc.lostPower();  // Store before any adjust() may clear it
  bool needsTimeSet = false;

  if (batteryWasDead) {
    needsTimeSet = true; // Battery was dead
  } else if (now.year() < 2020) {
    needsTimeSet = true; // Time is obviously wrong
  }

  if (needsTimeSet) {
    // Set RTC to compile time on upload
    // Using non-PROGMEM version to avoid uninitialized warning
    DateTime compileTime = DateTime(__DATE__, __TIME__);

    // Approximate offset to account for compile + upload time between __TIME__ being baked
    // into the binary and the RTC actually being set. Real upload time varies (typically
    // 5-15s on Arduino Uno); adjust if your toolchain is consistently faster/slower.
    const int UPLOAD_OFFSET_SECONDS = 10;

    DateTime adjustedTime = compileTime + TimeSpan(0, 0, 0, UPLOAD_OFFSET_SECONDS);
    rtc.adjust(adjustedTime);
  }
  
  // Load settings from EEPROM
  loadSettingsFromEEPROM();

  pinMode(RESW_PIN, INPUT_PULLUP);
  pinMode(REA_PIN, INPUT_PULLUP);
  pinMode(REB_PIN, INPUT_PULLUP);

  tubes[0].spi_init();

  // Power-on animation
  powerOnAnimation();

  // Initialize all tubes with loaded LED colors
  for (int i = 0; i < NUM_TUBES; i++) {
    tubes[i].clear();
    tubes[i].set_led(led_red, led_green, led_blue);
  }

  // Flash pattern to confirm time was set (AFTER tube init)
  if (batteryWasDead) {
    flashWarningPattern(); // Orange if battery was dead
  } else if (needsTimeSet) {
    flashSetConfirmation(); // Green flash to show time was programmed
  }
  // No flash if RTC had valid time already

  // Watchdog timer removed - was causing startup issues
  // If you need watchdog protection, enable it here after testing
  
  lastAntiPoisonCycle = millis();
}

void loop() {
  DateTime now = rtc.now();
  unsigned long currentMillis = millis();

  // Check for set mode timeout
  if (currentMode != NORMAL) {
    if (currentMillis - setModeEntryTime >= SET_MODE_TIMEOUT) {
      // Timeout - save settings and exit
      if (isTimeSetMode(currentMode)) {
        saveAndExit(); // Save time/date
      } else if (isValueSetMode(currentMode)) {
        saveSettingsToEEPROM(); // Save display settings
      }
      currentMode = NORMAL;
    }
  }

  // Anti-cathode poisoning cycle (non-blocking)
  if (currentMode == NORMAL && !isDateMode) {
    if (!isAntiPoisonRunning && currentMillis - lastAntiPoisonCycle >= ANTI_POISON_INTERVAL) {
      // Start the cycle
      isAntiPoisonRunning = true;
      antiPoisonDigit = 0;
      antiPoisonDigitTime = currentMillis;
    }

    if (isAntiPoisonRunning) {
      if (currentMillis - antiPoisonDigitTime >= ANTI_POISON_CYCLE_DURATION) {
        antiPoisonDigitTime = currentMillis;
        antiPoisonDigit++;

        if (antiPoisonDigit > 9) {
          // Cycle complete
          isAntiPoisonRunning = false;
          lastAntiPoisonCycle = currentMillis;
          for (int i = 0; i < NUM_TUBES; i++) {
            prev_digits[i] = -1; // Force refresh
          }
        }
      }

      if (isAntiPoisonRunning) {
        // Display current anti-poison digit on all tubes
        for (int i = 0; i < NUM_TUBES; i++) {
          tubes[i].show_digit(antiPoisonDigit, brightness, overdrive);
        }
      }
    }
  }

  // Handle switch press
  handleSwitchPress(currentMillis, now);

  // Poll the rotary encoder
  if (currentMillis - lastEncoderRead >= ENCODER_POLL_INTERVAL) {
    lastEncoderRead = currentMillis;
    readEncoder();
  }

  // Update target digits based on mode
  updateTargetDigits(now, currentMillis);

  // Adjust values based on encoder rotation
  adjustSetValue();

  // Update tube display at frame rate
  if (currentMillis - lastFrameTime >= FRAME_INTERVAL) {
    lastFrameTime = currentMillis;
    updateTubes(currentMillis);
  }
}

void loadSettingsFromEEPROM() {
  // Check if EEPROM has been initialized
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE) {
    brightness = EEPROM.read(EEPROM_BRIGHTNESS_ADDR);
    led_red = EEPROM.read(EEPROM_LED_RED_ADDR);
    led_green = EEPROM.read(EEPROM_LED_GREEN_ADDR);
    led_blue = EEPROM.read(EEPROM_LED_BLUE_ADDR);
    
    // Validate ranges
    brightness = constrain(brightness, 0, 127);
    led_red = constrain(led_red, 0, 127);
    led_green = constrain(led_green, 0, 127);
    led_blue = constrain(led_blue, 0, 127);
  } else {
    // First time setup - save defaults
    saveSettingsToEEPROM();
  }
}

void saveSettingsToEEPROM() {
  EEPROM.update(EEPROM_BRIGHTNESS_ADDR, brightness);
  EEPROM.update(EEPROM_LED_RED_ADDR, led_red);
  EEPROM.update(EEPROM_LED_GREEN_ADDR, led_green);
  EEPROM.update(EEPROM_LED_BLUE_ADDR, led_blue);
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
}

void powerOnAnimation() {
  // Slot machine style count-up animation
  
  for (int digit = 0; digit <= 9; digit++) {
    for (int i = 0; i < NUM_TUBES; i++) {
      tubes[i].show_digit(digit, brightness, overdrive);
    }
    delay(80);
  }
  
  // Clear all
  for (int i = 0; i < NUM_TUBES; i++) {
    tubes[i].clear();
  }
  delay(200);
}

void flashErrorPattern() {
  // Flash pattern for RTC failure
  
  for (int i = 0; i < NUM_TUBES; i++) {
    tubes[i].spi_init();
    tubes[i].set_led(127, 0, 0); // Red LEDs
  }
  
  for (int cycle = 0; cycle < 10; cycle++) {
    for (int i = 0; i < NUM_TUBES; i++) {
      tubes[i].show_digit(8, 127, 0); // Show all segments
    }
    delay(300);
    for (int i = 0; i < NUM_TUBES; i++) {
      tubes[i].clear();
    }
    delay(300);
  }
}

void flashWarningPattern() {
  // Flash pattern for RTC power loss
  
  for (int i = 0; i < NUM_TUBES; i++) {
    tubes[i].set_led(127, 64, 0); // Orange LEDs
  }
  
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int i = 0; i < NUM_TUBES; i++) {
      tubes[i].show_digit(0, 127, 0);
    }
    delay(500);
    for (int i = 0; i < NUM_TUBES; i++) {
      tubes[i].clear();
    }
    delay(500);
  }
}

void flashSetConfirmation() {
  // Green flash to confirm time was set from compile time
  
  for (int i = 0; i < NUM_TUBES; i++) {
    tubes[i].set_led(0, 127, 0); // Green LEDs
  }
  
  // Single quick flash
  for (int i = 0; i < NUM_TUBES; i++) {
    tubes[i].show_digit(8, 127, 0);
  }
  delay(200);
  for (int i = 0; i < NUM_TUBES; i++) {
    tubes[i].clear();
  }
  delay(200);
}

int daysInMonth(int month, int year) {
  // Returns max days for given month/year
  if (month == 2) {
    // Leap year check (simplified for 2000-2099)
    return ((year % 4 == 0) ? 29 : 28);
  }
  if (month == 4 || month == 6 || month == 9 || month == 11) {
    return 30;
  }
  return 31;
}

void handleSwitchPress(unsigned long currentMillis, DateTime now) {
  bool currentSwitchState = digitalRead(RESW_PIN) == LOW;
  
  if (currentSwitchState && !switchPressed) {
    switchPressTime = currentMillis;
    switchPressed = true;
  } else if (!currentSwitchState && switchPressed) {
    unsigned long pressDuration = currentMillis - switchPressTime;
    switchPressed = false;
    
    if (pressDuration < SHORT_PRESS_THRESHOLD) { // Short press
      // Date/time set chain: NORMAL -> YEAR -> MONTH -> DAY -> HOURS -> MINUTES -> SECONDS -> save & exit
      // Display set chain (entered via long press from NORMAL): BRIGHTNESS -> LED_RED -> LED_GREEN -> LED_BLUE -> save & exit
      if (currentMode == NORMAL) {
        currentMode = SET_YEAR;
        loadCurrentTime(now);
        setModeEntryTime = currentMillis;
      } else if (currentMode == SET_YEAR) {
        currentMode = SET_MONTH;
      } else if (currentMode == SET_MONTH) {
        currentMode = SET_DAY;
      } else if (currentMode == SET_DAY) {
        currentMode = SET_HOURS;
      } else if (currentMode == SET_HOURS) {
        currentMode = SET_MINUTES;
      } else if (currentMode == SET_MINUTES) {
        currentMode = SET_SECONDS;
      } else if (currentMode == SET_SECONDS) {
        // After setting seconds, save full date+time and exit
        saveAndExit(); // sets currentMode = NORMAL internally
      } else if (currentMode == SET_BRIGHTNESS) {
        currentMode = SET_LED_RED;
        updateLEDColorDisplay();
      } else if (currentMode == SET_LED_RED) {
        currentMode = SET_LED_GREEN;
        updateLEDColorDisplay();
      } else if (currentMode == SET_LED_GREEN) {
        currentMode = SET_LED_BLUE;
        updateLEDColorDisplay();
      } else if (currentMode == SET_LED_BLUE) {
        saveSettingsToEEPROM();
        currentMode = NORMAL; // Exit LED color mode
      }
    } else if (pressDuration > LONG_PRESS_THRESHOLD) { // Long press
      if (currentMode == NORMAL) {
        currentMode = SET_BRIGHTNESS;
        updateBrightnessDisplay();
        setModeEntryTime = currentMillis;
      } else {
        if (isTimeSetMode(currentMode)) {
          saveAndExit();
        } else if (isValueSetMode(currentMode)) {
          saveSettingsToEEPROM();
        }
        currentMode = NORMAL; // Exit set mode
      }
    }
  }
}

void updateTargetDigits(DateTime now, unsigned long currentMillis) {
  // Normal mode: update time/date
  if (currentMode == NORMAL) {
    if (!isDateMode && !isAntiPoisonRunning) {
      uint8_t hrs = now.hour();
      uint8_t mins = now.minute();
      uint8_t secs = now.second();
      target_digits[0] = hrs / 10;
      target_digits[1] = hrs % 10;
      target_digits[2] = mins / 10;
      target_digits[3] = mins % 10;
      target_digits[4] = secs / 10;
      target_digits[5] = secs % 10;
    }

    // Switch to date display
    if (currentMillis - previousMillis >= DATE_DISPLAY_INTERVAL && !isDateMode && !isAntiPoisonRunning) {
      previousMillis = currentMillis;
      isDateMode = true;
      uint8_t yrs = now.year() % 100;
      uint8_t mth = now.month();
      uint8_t dys = now.day();
      target_digits[0] = dys / 10;
      target_digits[1] = dys % 10;
      target_digits[2] = mth / 10;
      target_digits[3] = mth % 10;
      target_digits[4] = yrs / 10;
      target_digits[5] = yrs % 10;
    } else if (isDateMode && currentMillis - previousMillis >= DATE_SHOW_DURATION) {
      isDateMode = false;
    }

    // Refresh LEDs periodically (with current LED colors)
    if (currentMillis - lastLedRefresh >= LED_REFRESH_INTERVAL) {
      lastLedRefresh = currentMillis;
      for (int i = 0; i < NUM_TUBES; i++) {
        tubes[i].set_led(led_red, led_green, led_blue);
      }
    }
  } else if (isTimeSetMode(currentMode)) {
    // Set time/date mode
    target_digits[0] = setValues.hour / 10;
    target_digits[1] = setValues.hour % 10;
    target_digits[2] = setValues.minute / 10;
    target_digits[3] = setValues.minute % 10;
    target_digits[4] = setValues.second / 10;
    target_digits[5] = setValues.second % 10;
    
    if (currentMode == SET_YEAR || currentMode == SET_MONTH || currentMode == SET_DAY) {
      // Date set mode mirrors normal-mode date readout layout: DD-MM-YY
      target_digits[0] = setValues.day / 10;
      target_digits[1] = setValues.day % 10;
      target_digits[2] = setValues.month / 10;
      target_digits[3] = setValues.month % 10;
      target_digits[4] = setValues.year / 10;
      target_digits[5] = setValues.year % 10;
    }
  }
  // Brightness and LED color modes handled in their respective update functions
}

void readEncoder() {
  // Read both pins
  int MSB = digitalRead(REA_PIN);
  int LSB = digitalRead(REB_PIN);

  // Update state tracker (keep last 2 readings)
  encoderState = ((encoderState << 2) | (MSB << 1) | LSB) & 0x0F;

  // Only process if in a set mode
  if (currentMode != NORMAL) {
    int8_t direction = ENCODER_TABLE[encoderState];

    // Accumulate quadrature transitions; emit one count per full detent (4 sub-steps).
    // The state machine itself rejects contact bounce (invalid transitions return 0).
    if (direction != 0) {
      encoderSubStep += direction;
      if (encoderSubStep >= 4) {
        encoderPos++;
        encoderSubStep = 0;
      } else if (encoderSubStep <= -4) {
        encoderPos--;
        encoderSubStep = 0;
      }
    }
  }
}

void updateBrightnessDisplay() {
  // Blank first 3 digits, show brightness in last 3
  target_digits[0] = 0;
  target_digits[1] = 0;
  target_digits[2] = 0;
  target_digits[3] = brightness / 100;
  target_digits[4] = (brightness / 10) % 10;
  target_digits[5] = brightness % 10;
}

void updateLEDColorDisplay() {
  // Display current LED color value being adjusted
  uint8_t value = 0;
  if (currentMode == SET_LED_RED) value = led_red;
  else if (currentMode == SET_LED_GREEN) value = led_green;
  else if (currentMode == SET_LED_BLUE) value = led_blue;
  
  target_digits[0] = 0;
  target_digits[1] = 0;
  target_digits[2] = 0;
  target_digits[3] = value / 100;
  target_digits[4] = (value / 10) % 10;
  target_digits[5] = value % 10;
  
  // Update LED color in real-time
  for (int i = 0; i < NUM_TUBES; i++) {
    tubes[i].set_led(led_red, led_green, led_blue);
  }
}

void adjustSetValue() {
  static int lastPos = 0;
  int delta = encoderPos - lastPos;
  
  if (delta != 0) {
    int8_t step = constrain(delta, -3, 3); // range -3..+3
    
    if (currentMode == SET_BRIGHTNESS) {
      brightness = constrain(brightness + step, 0, 127);
      updateBrightnessDisplay();
    } else if (currentMode == SET_LED_RED) {
      led_red = constrain(led_red + step, 0, 127);
      updateLEDColorDisplay();
    } else if (currentMode == SET_LED_GREEN) {
      led_green = constrain(led_green + step, 0, 127);
      updateLEDColorDisplay();
    } else if (currentMode == SET_LED_BLUE) {
      led_blue = constrain(led_blue + step, 0, 127);
      updateLEDColorDisplay();
    } else if (currentMode != NORMAL) {
      switch (currentMode) {
        case SET_HOURS:
          setValues.hour = (setValues.hour + step + 24) % 24;
          break;
        case SET_MINUTES:
          setValues.minute = (setValues.minute + step + 60) % 60;
          break;
        case SET_SECONDS:
          setValues.second = (setValues.second + step + 60) % 60;
          break;
        case SET_DAY: {
          int maxDays = daysInMonth(setValues.month, setValues.year);
          int newDay = setValues.day + step;
          if (newDay > maxDays) newDay = 1;
          if (newDay < 1) newDay = maxDays;
          setValues.day = newDay;
          break;
        }
        case SET_MONTH: {
          int newMonth = setValues.month + step;
          if (newMonth > 12) newMonth = 1;
          if (newMonth < 1) newMonth = 12;
          setValues.month = newMonth;
          // Adjust day if it exceeds new month's max
          int maxDays = daysInMonth(setValues.month, setValues.year);
          if (setValues.day > maxDays) {
            setValues.day = maxDays;
          }
          break;
        }
        case SET_YEAR:
          setValues.year = (setValues.year + step + 100) % 100;
          // Check if day needs adjustment (leap year change)
          if (setValues.month == 2) {
            int maxDays = daysInMonth(2, setValues.year);
            if (setValues.day > maxDays) {
              setValues.day = maxDays;
            }
          }
          break;
        default:
          break;
      }
    }

    // Mark digits as changed to skip crossfade animation
    // Applies to all set modes (time/date, brightness, LED) for symmetry/safety.
    if (currentMode != NORMAL) {
      for (int i = 0; i < NUM_TUBES; i++) {
        prev_digits[i] = -1;
      }
    }

    lastPos = encoderPos;
  }
}

void loadCurrentTime(DateTime now) {
  setValues.hour = now.hour();
  setValues.minute = now.minute();
  setValues.second = now.second();
  setValues.day = now.day();
  setValues.month = now.month();
  setValues.year = now.year() % 100;
}

void saveTimeOnly() {
  // Save time only, keep existing date in RTC
  DateTime current = rtc.now();
  DateTime newTime(current.year(), current.month(), current.day(),
                   setValues.hour, setValues.minute, setValues.second);
  rtc.adjust(newTime);
}

void saveAndExit() {
  DateTime newTime(2000 + setValues.year, setValues.month, setValues.day,
                   setValues.hour, setValues.minute, setValues.second);
  rtc.adjust(newTime);
  currentMode = NORMAL;
}

// ===== Mode classification helpers =====
bool isValueSetMode(SetMode m) {
  return m == SET_BRIGHTNESS || m == SET_LED_RED || m == SET_LED_GREEN || m == SET_LED_BLUE;
}

bool isTimeSetMode(SetMode m) {
  return m == SET_HOURS || m == SET_MINUTES || m == SET_SECONDS ||
         m == SET_DAY   || m == SET_MONTH   || m == SET_YEAR;
}

// Returns starting digit index of the blinking pair for time/date set modes,
// or -1 if no blink applies.
int8_t blinkPairStart(SetMode m) {
  switch (m) {
    case SET_HOURS:
    case SET_DAY:     return 0;
    case SET_MINUTES:
    case SET_MONTH:   return PAIR_FIRST_END;
    case SET_SECONDS:
    case SET_YEAR:    return PAIR_SECOND_END;
    default:          return -1;
  }
}

// ===== Render helpers =====
void renderNormalMode() {
  for (int i = 0; i < NUM_TUBES; i++) {
    if (prev_digits[i] != target_digits[i]) {
      tubes[i].crossfade_init(target_digits[i], CROSSFADE_DURATION, brightness, overdrive);
      prev_digits[i] = target_digits[i];
    }
    tubes[i].crossfade_run();
  }
}

void renderValueSetMode() {
  // First VALUE_DIGIT_START tubes blank; remaining show value digits immediately.
  for (int i = 0; i < NUM_TUBES; i++) {
    if (i < VALUE_DIGIT_START) {
      tubes[i].clear();
      prev_digits[i] = -1;
    } else {
      tubes[i].show_digit(target_digits[i], brightness, overdrive);
      prev_digits[i] = target_digits[i];
    }
  }
}

void renderTimeSetMode() {
  int8_t blinkStart = blinkPairStart(currentMode);
  for (int i = 0; i < NUM_TUBES; i++) {
    bool shouldBlink = (blinkStart >= 0 && i >= blinkStart && i < blinkStart + 2);
    if (shouldBlink && !blinkState) {
      tubes[i].clear();
    } else {
      tubes[i].show_digit(target_digits[i], brightness, overdrive);
    }
    prev_digits[i] = target_digits[i];
  }
}

void updateBlinkState(unsigned long currentMillis) {
  if (currentMillis - lastBlinkTime >= BLINK_INTERVAL) {
    lastBlinkTime = currentMillis;
    blinkState = !blinkState;
  }
}

// ===== updateTubes entry point =====
void updateTubes(unsigned long currentMillis) {
  if (isAntiPoisonRunning) return;

  if (currentMode == NORMAL) {
    renderNormalMode();
    return;
  }

  if (isValueSetMode(currentMode)) {
    renderValueSetMode();
    return;
  }

  // Time/date set mode (only remaining case)
  updateBlinkState(currentMillis);
  renderTimeSetMode();
}
