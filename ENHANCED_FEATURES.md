# Nixie Clock - Enhanced Version Documentation

## Overview
This enhanced version includes all improvements from the previous version plus:
- Date validation with leap year support
- EEPROM persistence for brightness and LED colors
- Anti-cathode poisoning protection
- Watchdog timer for reliability
- Cleaner encoder state machine
- RTC power loss detection
- Power-on animation
- Set mode timeout
- LED color customization
- Memory optimizations

## New Features Detail

### 1. Date Validation ✓
**Function:** `daysInMonth(int month, int year)`
- Validates day settings based on actual month/year
- Handles leap years correctly (2000-2099 range)
- Auto-adjusts day when changing month (e.g., Jan 31 → Feb 28)
- Prevents invalid dates like Feb 30 or Apr 31

**Example:**
- Setting Feb 30 → automatically limited to Feb 28 (or 29 in leap years)
- Changing month from Jan to Feb while on day 31 → auto-adjusts to Feb 28/29

### 2. EEPROM Persistence ✓
**Addresses:**
- `0`: Brightness (0-127)
- `1`: LED Red (0-255)
- `2`: LED Green (0-255)
- `3`: LED Blue (0-255)
- `4`: Magic byte (0xA5) - indicates EEPROM is initialized

**Behavior:**
- Settings loaded on startup
- Settings saved when exiting brightness/LED color modes
- First boot uses defaults and saves them
- Survives power cycles

**Functions:**
- `loadSettingsFromEEPROM()` - Called in setup()
- `saveSettingsToEEPROM()` - Called on mode exit

### 3. Anti-Cathode Poisoning ✓
**How it works:**
- Runs every 6 hours automatically (configurable)
- Cycles through digits 0-9 on all tubes
- Each digit shown for 100ms
- Total cycle duration: ~1 second
- Prevents unused cathodes from deteriorating

**Configuration:**
```cpp
const unsigned long ANTI_POISON_INTERVAL = 21600000; // 6 hours
const int ANTI_POISON_CYCLE_DURATION = 100; // ms per digit
```

**Function:** `runAntiPoisonCycle()`

### 4. Watchdog Timer ✓
**Purpose:** Auto-reset if code hangs
**Timeout:** 8 seconds
**Implementation:**
- Enabled in `setup()`
- Reset every loop iteration via `wdt_reset()`
- Also reset during anti-poison cycle
- Prevents permanent hangs

### 5. Improved Encoder Logic ✓
**Old method:** Manual state checking with duplicate conditions
**New method:** State lookup table

**Benefits:**
- No duplicate state values (fixed the 0b1011 bug)
- Cleaner, more reliable
- Easier to debug
- Better rotation detection

**Implementation:**
```cpp
const int8_t ENCODER_TABLE[] = {
  0, -1, 1, 0,   // State 00
  1, 0, 0, -1,   // State 01
  -1, 0, 0, 1,   // State 10
  0, 1, -1, 0    // State 11
};
```

### 6. RTC Lost Power Detection ✓
**Detection:** `rtc.lostPower()` in setup()
**Actions if power lost:**
1. Sets default time: Jan 1, 2024, 00:00:00
2. Flashes orange warning pattern
3. Allows normal operation

**Warning Pattern:**
- Orange LEDs (255, 127, 0)
- 3 flashes
- All tubes show "0"
- 500ms on/off

**Error Pattern (RTC failure):**
- Red LEDs (255, 0, 0)
- 10 flashes
- All tubes show "8" (all segments)
- 300ms on/off
- System halts

### 7. Power-On Animation ✓
**Function:** `powerOnAnimation()`
**Animation:**
- Slot machine style count-up
- All tubes count 0-9 simultaneously
- 80ms per digit
- 200ms blank at end
- Total duration: ~1 second

**Purpose:** Visual feedback that clock initialized successfully

### 8. Set Mode Timeout ✓
**Timeout:** 5 minutes (300,000ms)
**Behavior:**
- Applies to all set modes
- Auto-saves time/date if in SET_HOURS through SET_YEAR
- Auto-saves settings if in SET_BRIGHTNESS or LED color modes
- Returns to NORMAL mode
- Prevents accidentally leaving clock in set mode

**Configuration:**
```cpp
const unsigned long SET_MODE_TIMEOUT = 300000; // 5 minutes
```

### 9. LED Color Customization ✓
**New Modes:**
- `SET_LED_RED` - Adjust red component (0-255)
- `SET_LED_GREEN` - Adjust green component (0-255)
- `SET_LED_BLUE` - Adjust blue component (0-255)

**Navigation:**
From NORMAL mode:
1. Long press → SET_BRIGHTNESS
2. Short press → SET_LED_RED
3. Short press → SET_LED_GREEN
4. Short press → SET_LED_BLUE
5. Short press → Exit and save to EEPROM

OR long press from any LED mode to save and exit

**Display Format:**
- First 3 digits: blank
- Last 3 digits: value (000-255)
- Example: "   127" for value 127

**Live Preview:** LEDs update in real-time as you adjust colors

### 10. Memory Optimization ✓
**DateTime Struct:**
```cpp
struct DateTimeSet {
  uint8_t hour;    // 0-23
  uint8_t minute;  // 0-59
  uint8_t second;  // 0-59
  uint8_t day;     // 1-31
  uint8_t month;   // 1-12
  uint8_t year;    // 0-99 (last 2 digits)
} setValues;
```

**Benefits:**
- Uses uint8_t instead of int (saves RAM)
- Clearer code organization
- Access via `setValues.hour` instead of `setValues[0]`

### 11. Smoother Crossfade ✓
**Change:** Reduced from 15 frames to 10 frames
**Duration:** 330ms instead of 495ms
**Benefit:** Faster animations, no overlap between seconds

## Usage Guide

### Basic Operation
- Clock displays time by default
- Switches to date display every 55 seconds for 5 seconds
- Anti-poison cycle runs every 6 hours automatically

### Button Controls

**Short Press (<200ms):**
- From NORMAL: Enter time/date setting (SET_HOURS)
- During setting: Advance to next field
- From SET_BRIGHTNESS: Enter LED color mode (SET_LED_RED)

**Long Press (>1000ms):**
- From NORMAL: Enter brightness adjustment
- During setting: Save and exit to NORMAL

### Setting Time/Date Sequence
1. Short press → SET_HOURS (hours blink)
2. Rotate encoder to adjust
3. Short press → SET_MINUTES (minutes blink)
4. Rotate encoder to adjust
5. Short press → SET_SECONDS (seconds blink)
6. Rotate encoder to adjust
7. Short press → SET_DAY (day blinks)
8. Rotate encoder to adjust
9. Short press → SET_MONTH (month blinks)
10. Rotate encoder to adjust
11. Short press → SET_YEAR (year blinks)
12. Rotate encoder to adjust
13. Short press → SET_BRIGHTNESS
14. Long press → Save and exit

### Setting Brightness
1. Long press from NORMAL
2. Rotate encoder (range: 0-127)
3. Display shows "    XX"
4. Short press to LED color mode or long press to save and exit

### Setting LED Colors
1. From brightness mode, short press through:
   - SET_LED_RED (displays "   XXX")
   - SET_LED_GREEN (displays "   XXX")
   - SET_LED_BLUE (displays "   XXX")
2. Rotate encoder to adjust each (0-255)
3. LEDs update in real-time
4. Short press after blue exits and saves
5. OR long press anytime to save and exit

### Auto-Features
- **5-minute timeout:** If no button pressed in set mode, auto-saves and exits
- **Anti-poison:** Runs every 6 hours in NORMAL mode
- **Watchdog:** Auto-resets if system hangs (8 second timeout)

## EEPROM Memory Map
```
Address | Content           | Range
--------|-------------------|--------
0       | Brightness        | 0-127
1       | LED Red           | 0-255
2       | LED Green         | 0-255
3       | LED Blue          | 0-255
4       | Magic Byte (0xA5) | Initialized flag
```

## Constants Reference

### Timing
```cpp
DATE_DISPLAY_INTERVAL = 55000      // Time before showing date (55s)
DATE_SHOW_DURATION = 5000          // How long to show date (5s)
FRAME_INTERVAL = 33                // Display refresh rate (33ms)
LED_REFRESH_INTERVAL = 1000        // LED refresh rate (1s)
ENCODER_POLL_INTERVAL = 2          // Encoder read rate (2ms)
ENCODER_DEBOUNCE_DELAY = 10        // Encoder debounce (10ms)
SHORT_PRESS_THRESHOLD = 200        // Short press max (200ms)
LONG_PRESS_THRESHOLD = 1000        // Long press min (1s)
BLINK_INTERVAL = 500               // Blink rate in set mode (500ms)
CROSSFADE_DURATION = 10            // Crossfade frames (10 = 330ms)
SET_MODE_TIMEOUT = 300000          // Auto-exit set mode (5 min)
ANTI_POISON_INTERVAL = 21600000    // Anti-poison period (6 hours)
ANTI_POISON_CYCLE_DURATION = 100   // Time per digit (100ms)
```

### Pin Definitions
```cpp
cs1-6 = 10, 9, 8, 7, 6, 5          // Nixie tube chip selects
REA_PIN = 2                         // Encoder A (D2)
REB_PIN = 3                         // Encoder B (D3)
RESW_PIN = A0                       // Encoder switch
```

## Troubleshooting

### RTC Won't Initialize
**Symptom:** Clock flashes red pattern 10 times then halts
**Cause:** RTC not responding on I2C bus
**Solutions:**
- Check RTC connections (SDA/SCL)
- Verify RTC power (VCC/GND)
- Check I2C pullup resistors

### Clock Shows Wrong Time After Power Cycle
**Symptom:** Time resets to Jan 1, 2024 00:00:00
**Cause:** RTC battery dead or missing
**Indication:** Orange flash pattern on startup
**Solution:** Replace CR2032 battery in DS3231

### Encoder Not Responsive
**Symptom:** Rotating encoder doesn't change values
**Cause:** Only active in set modes
**Solution:** Press button to enter a set mode first

### Settings Don't Save
**Symptom:** Brightness/colors reset after power cycle
**Cause:** EEPROM not saving
**Check:**
- Look for `saveSettingsToEEPROM()` calls
- Verify you exited mode properly (not just unplugged)
- EEPROM may have write protection (rare)

### Tubes Flash Randomly
**Symptom:** Random digits appear briefly
**Cause:** Normal - anti-cathode poisoning cycle
**Frequency:** Every 6 hours
**Duration:** ~1 second
**Normal behavior:** Preserves tube life

### System Resets Randomly
**Symptom:** Clock restarts unexpectedly
**Cause:** Watchdog timer triggered
**Meaning:** Code got stuck somewhere
**Check:** Look for infinite loops or blocking operations

## Performance Notes

### RAM Usage
- Struct optimization saves ~6 bytes
- Static tubes array saves frequent allocations
- Total RAM usage: ~200 bytes (well within Arduino limits)

### Animation Performance
- 10-frame crossfade = 330ms
- Runs at 30 fps (33ms per frame)
- No overlap with 1-second intervals
- Smooth visual transitions

### Encoder Responsiveness
- 2ms polling = 500 checks per second
- 10ms debounce prevents bounce
- State table eliminates errors
- Very responsive feel

## Future Enhancement Ideas

### Already Implemented
- ✓ Date validation
- ✓ EEPROM persistence
- ✓ Anti-cathode poisoning
- ✓ Watchdog timer
- ✓ Improved encoder
- ✓ RTC power loss detection
- ✓ Power-on animation
- ✓ Set mode timeout
- ✓ LED color customization
- ✓ Memory optimization
- ✓ Smoother crossfade

### Potential Future Additions
- Temperature display (DS3231 has sensor)
- Alarm function
- Multiple time zones
- Custom animation effects
- Configurable anti-poison intervals
- Serial debug output
- RGB cycling modes
- Configurable date display format order

## Migration from Previous Version

### What's New
- Add `#include <EEPROM.h>` and `#include <avr/wdt.h>`
- New EEPROM functions and constants
- DateTime struct instead of array
- LED color adjustment modes
- Anti-poison cycle
- Watchdog timer
- Enhanced error handling

### What's Compatible
- Same pin assignments
- Same hardware requirements
- Same basic operation
- Same exixe library

### What Changed
- `setValues` is now a struct (`.hour` instead of `[0]`)
- LED colors now adjustable and saved
- Brightness now persistent across power cycles
- Additional set modes for LED colors

## Code Size
- Program storage: ~12-14 KB (depending on optimization)
- Dynamic memory: ~200 bytes
- EEPROM usage: 5 bytes
- Plenty of room on Arduino Uno/Nano

## Testing Checklist

- [ ] Power-on animation plays correctly
- [ ] Time displays and updates every second
- [ ] Date displays every 55 seconds for 5 seconds
- [ ] RTC power loss detection (remove battery briefly)
- [ ] Short press enters SET_HOURS
- [ ] Encoder adjusts hours correctly
- [ ] Progress through all time/date fields
- [ ] Date validation (try setting Feb 30)
- [ ] Long press saves and exits
- [ ] Long press from NORMAL enters brightness mode
- [ ] Brightness adjusts 0-127
- [ ] Short press from brightness enters LED_RED
- [ ] LED colors adjust and show real-time preview
- [ ] LED colors persist after power cycle
- [ ] Brightness persists after power cycle
- [ ] 5-minute timeout auto-saves and exits
- [ ] Anti-poison cycle runs (can trigger manually by waiting 6 hours or changing constant)
- [ ] Watchdog prevents hangs
- [ ] All tubes display correctly
- [ ] LED colors uniform across all tubes

## Credits & License
Enhanced nixie clock firmware with comprehensive features for reliability and customization.
Compatible with exixe nixie tube driver modules and DS3231 RTC.
