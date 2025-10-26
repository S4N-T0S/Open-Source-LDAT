#pragma once

// --- Pinout Configuration ---
// Must use digital pins that support interrupts for the button.
const int PIN_BUTTON = 4;        // Push to make button for menu navigation
const int PIN_SEND_CLICK = 5;    // Pin to send a mouse click signal (output)
const int PIN_RECEIVE_CLICK = 6; // Pin to receive a mouse click signal from the mouse (input)
const int PIN_LED_BUILTIN = 13;    // Built-in LED for error indication

// Analog pin for the light sensor
const int PIN_LIGHT_SENSOR = 23;

// I2C pins for the OLED display (Wire)
// Teensy 4.1 default I2C pins are 18 (SDA) and 19 (SCL)

// --- Behavior and Thresholds ---
const unsigned long BUTTON_HOLD_DURATION_MS = 2000; // Time in ms to hold button for select
const int LIGHT_SENSOR_THRESHOLD = 0;  // Value above which light is considered "detected"
const int LIGHT_SENSOR_ERROR_MAX = 300; // Max value on boot, otherwise sensor error
const float MOUSE_PRESENCE_VOLTAGE = 0.5f; // Voltage threshold to detect mouse presence

// --- Display Configuration ---
const int SCREEN_WIDTH = 128; // OLED display width, in pixels
const int SCREEN_HEIGHT = 64; // OLED display height, in pixels
const int OLED_RESET = -1; // Reset pin # (or -1 if sharing Arduino reset pin)
const char* GITHUB_TAG = "GitHub: S4N-T0S";

// --- Timing Configuration ---
// Add a small delay after sending a click in auto mode to prevent accidental re-triggers
const unsigned long AUTO_MODE_CLICK_DELAY_MS = 100;