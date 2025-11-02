#pragma once

// --- Pinout Configuration ---
// Must use digital pins that support interrupts for the button.
const int PIN_BUTTON = 4; // Push to make button for menu navigation
const int PIN_SEND_CLICK = 5; // Pin to send a mouse click signal (output)

const int PIN_MOUSE_PRESENCE = 21; // Analog pin to detect mouse presence (input) (3.3V~)

// --- Mouse Presence Check Configuration (8-bit ADC: 0-255) ---
// The check now verifies two conditions:
// 1. The voltage is high enough (at least MOUSE_PRESENCE_MIN_ADC_VALUE).
// 2. The voltage is stable (fluctuation is less than MOUSE_STABILITY_THRESHOLD_ADC).

// This sets the minimum acceptable ADC value. 3.0V on a 3.3V system with an 8-bit ADC.
// Calculation: (3.0 / 3.3) * 255 = ~231.
const int MOUSE_PRESENCE_MIN_ADC_VALUE = 230;

// This sets the maximum allowed fluctuation, equivalent to ~200mV.
// Calculation: (0.2 / 3.3) * 255 = ~15.
const int MOUSE_STABILITY_THRESHOLD_ADC = 15;

// This sets how long the script will hold the mouse click signal when sending via USB,
// the higher this number, the higher latency you will see added from this value.
// The Teensy 4.1 has been modified to run at 8000 Hz polling rate for USB HID devices. So this value should be set to 250 for Direct modes, or to minmax 150.
// >>>>>>>>>>> PLEASE SET IT TO A VALUE CLOSEST TO YOUR MOUSE POLLING RATE! <<<<<<<<<<

// SAFE AND ROBUST VALUES (I would lower from these guidelines only if you are limit testing):
// For example if I have a very reliable mouse/system, I might set my value to just over the polling rate, e.g., 135 for 8kHz polling rate.
// At 8k Hz polling rate, each poll is 125 microseconds, so holding for 2 of these polls is very safe (250 microseconds)
// At 4k Hz polling rate, each poll is 250 microseconds... (500 microseconds)
// At 2k Hz polling rate, each poll is 500 microseconds... (1000 microseconds)
// At 1k Hz polling rate, each poll is 1000 microseconds... (2000 microseconds)
// At 500 Hz polling rate, each poll is 2000 microseconds... (4000 microseconds)
// At 250 Hz polling rate, each poll is 4000 microseconds... (8000 microseconds)
const int MOUSE_CLICK_HOLD_MICROS = 140;
// ^ IMPORTANT: This delay is only for UE4 modes. Automatic mode holds down the click until it detects LIGHT_SENSOR_THRESHOLD, then lets go.

const int PIN_LED_BUILTIN = 13; // Built-in LED for error indication

// Analog pin for the light sensor
const int PIN_LIGHT_SENSOR = 23;

// Light sensor thresholds (I tested this on an OLED, so my dark would get to essentially between 0-3 and my light around 150+)
const int LIGHT_SENSOR_THRESHOLD = 15; // Value above which light is considered "detected"
const int DARK_SENSOR_THRESHOLD = 10; // Value below which darkness is considered "detected"
const int SENSOR_FLUCTUATION_THRESHOLD = 35; // Max allowed change during stability check

// --- Behavior Settings ---
const unsigned long BUTTON_HOLD_START_MS = 250; // Time in ms to start showing hold action
const unsigned long BUTTON_HOLD_DURATION_MS = 750; // Time in ms to hold button for SELECT
const unsigned long BUTTON_DEBUG_DURATION_MS = 1250; // Time in ms to hold button for DEBUG
const unsigned long BUTTON_RESET_DURATION_MS = 1750; // Time in ms to hold for a global RESET
const unsigned long FLUC_CHECK_DURATION_MS = 1500; // Duration to check sensor/mouse stability

// --- Display Configuration --- I2C pins for the OLED display (Wire) = Teensy 4.1 default I2C pins are 18 (SDA) and 19 (SCL)
const int SCREEN_WIDTH = 128; // OLED display width, in pixels
const int SCREEN_HEIGHT = 64; // OLED display height, in pixels
const byte SCREEN_ADDRESS = 0x3C; // I2C address for the OLED display, could be 0x3C or 0x3D depending on the module
const int OLED_RESET = -1; // Reset pin # (or -1 if sharing Arduino reset pin)
const char* GITHUB_TAG = "GitHub: S4N-T0S";

// --- Timing Configuration ---
// Add a delay between runs to allow system to stabilize and, allow the monitor to dim back.
const unsigned long AUTO_MODE_RUN_DELAY_MS = 750;
const int MODE_DELAY_JITTER_MS = 10;
const unsigned long UE4_MODE_RUN_DELAY_MS = 250;

// --- Run Limit Configuration ---
// These values populate the "Select Run Limit" menu.
const unsigned long RUN_LIMIT_OPTION_1 = 100;
const unsigned long RUN_LIMIT_OPTION_2 = 300;
const unsigned long RUN_LIMIT_OPTION_3 = 500;