#include "exixe.h"
#include "RTClib.h"
#include <EEPROM.h>
// WATCHDOG DISABLED FOR DIAGNOSTIC

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
int brightness = 55;
const int overdrive = 0;

// Nixie tube objects
exixe my_tube1 = exixe(cs1);
exixe my_tube2 = exixe(cs2);
exixe my_tube3 = exixe(cs3);
exixe my_tube4 = exixe(cs4);
exixe my_tube5 = exixe(cs5);
exixe my_tube6 = exixe(cs6);

// RGB LED values
int led_red = 127;
int led_green = 0;
int led_blue = 127;

// Rotary encoder pins
#define REA_PIN 2
#define REB_PIN 3
#define RESW_PIN A0

void setup() {
  // DIAGNOSTIC: Try to get SOMETHING working
  
  pinMode(RESW_PIN, INPUT_PULLUP);
  pinMode(REA_PIN, INPUT_PULLUP);
  pinMode(REB_PIN, INPUT_PULLUP);

  my_tube1.spi_init();
  
  // Load EEPROM settings
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE) {
    brightness = constrain(EEPROM.read(EEPROM_BRIGHTNESS_ADDR), 0, 127);
    led_red = constrain(EEPROM.read(EEPROM_LED_RED_ADDR), 0, 255);
    led_green = constrain(EEPROM.read(EEPROM_LED_GREEN_ADDR), 0, 255);
    led_blue = constrain(EEPROM.read(EEPROM_LED_BLUE_ADDR), 0, 255);
  }
  
  // Initialize tubes - MINIMAL
  my_tube1.set_led(led_red, led_green, led_blue);
  my_tube1.show_digit(1, brightness, overdrive);
  
  my_tube2.set_led(led_red, led_green, led_blue);
  my_tube2.show_digit(2, brightness, overdrive);
  
  my_tube3.set_led(led_red, led_green, led_blue);
  my_tube3.show_digit(3, brightness, overdrive);
  
  my_tube4.set_led(led_red, led_green, led_blue);
  my_tube4.show_digit(4, brightness, overdrive);
  
  my_tube5.set_led(led_red, led_green, led_blue);
  my_tube5.show_digit(5, brightness, overdrive);
  
  my_tube6.set_led(led_red, led_green, led_blue);
  my_tube6.show_digit(6, brightness, overdrive);
  
  delay(2000); // Hold for 2 seconds
  
  // NOW try RTC
  if (!rtc.begin()) {
    // RTC failed - show 8s
    for (int i = 0; i < 6; i++) {
      exixe* tubes[6] = {&my_tube1, &my_tube2, &my_tube3, &my_tube4, &my_tube5, &my_tube6};
      tubes[i]->show_digit(8, brightness, overdrive);
    }
    while(1); // Halt
  }
  
  // Set RTC time
  DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
  const int UPLOAD_OFFSET_SECONDS = 10;
  DateTime adjustedTime = compileTime + TimeSpan(0, 0, 0, UPLOAD_OFFSET_SECONDS);
  rtc.adjust(adjustedTime);
  
  // Show 0s to indicate success
  my_tube1.show_digit(0, brightness, overdrive);
  my_tube2.show_digit(0, brightness, overdrive);
  my_tube3.show_digit(0, brightness, overdrive);
  my_tube4.show_digit(0, brightness, overdrive);
  my_tube5.show_digit(0, brightness, overdrive);
  my_tube6.show_digit(0, brightness, overdrive);
  
  delay(1000);
}

void loop() {
  // DIAGNOSTIC: Just display current time, nothing fancy
  DateTime now = rtc.now();
  
  int hrs = now.hour();
  int mins = now.minute();
  int secs = now.second();
  
  my_tube1.show_digit(hrs / 10, brightness, overdrive);
  my_tube2.show_digit(hrs % 10, brightness, overdrive);
  my_tube3.show_digit(mins / 10, brightness, overdrive);
  my_tube4.show_digit(mins % 10, brightness, overdrive);
  my_tube5.show_digit(secs / 10, brightness, overdrive);
  my_tube6.show_digit(secs % 10, brightness, overdrive);
  
  delay(100); // Update 10 times per second
}
