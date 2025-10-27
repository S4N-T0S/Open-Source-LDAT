#pragma once

// --- Pinout Configuration ---
// Must use digital pins that support interrupts for the button.
const int PIN_BUTTON = 4; // Push to make button for menu navigation
const int PIN_SEND_CLICK = 5; // Pin to send a mouse click signal (output)

const int PIN_MOUSE_PRESENCE = 21; // Analog pin to detect mouse presence (input) (For me it's optimal switch so 1Volt) - This pin can be used to detect if the mouse is clicked if wired to analog switch instead of optical.
const int MOUSE_FLUCTUATION_THRESHOLD = 200; // Max allowed change during stability check for mouse presence

const int PIN_LED_BUILTIN = 13; // Built-in LED for error indication

// Analog pin for the light sensor
const int PIN_LIGHT_SENSOR = 23;

// Light sensor settings
const int LIGHT_SENSOR_THRESHOLD = 100; // Value above which light is considered "detected"
const int DARK_SENSOR_THRESHOLD = 90; // Value below which darkness is considered "detected"
const int SENSOR_FLUCTUATION_THRESHOLD = 20; // Max allowed change during stability check

// I2C pins for the OLED display (Wire)
// Teensy 4.1 default I2C pins are 18 (SDA) and 19 (SCL)

// --- Behavior and Thresholds ---
const unsigned long BUTTON_HOLD_START_MS = 250; // Time in ms to start showing hold action
const unsigned long BUTTON_HOLD_DURATION_MS = 1000; // Time in ms to hold button for select
const unsigned long BUTTON_RESET_DURATION_MS = 2000; // Time in ms to hold for a global reset
const float MOUSE_PRESENCE_VOLTAGE = 0.5f; // Voltage threshold to detect mouse presence
const unsigned long FLUC_CHECK_DURATION_MS = 1500; // Duration to check sensor/mouse stability


// --- Display Configuration ---
const int SCREEN_WIDTH = 128; // OLED display width, in pixels
const int SCREEN_HEIGHT = 64; // OLED display height, in pixels
const int OLED_RESET = -1; // Reset pin # (or -1 if sharing Arduino reset pin)
const char* GITHUB_TAG = "GitHub: S4N-T0S";

// --- Timing Configuration ---
// Add a small delay after sending a click in auto mode to prevent accidental re-triggers
const unsigned long AUTO_MODE_CLICK_DELAY_MS = 150;