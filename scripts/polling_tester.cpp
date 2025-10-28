#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../include/config.h"

// This sketch uses a lower-level USB function to ensure maximum speed.
// It is designed to test the 8kHz polling rate capability of the Teensy 4.1.
// The polling rate is set to 8kHz (125us interval) via the `8kHz_polling.py`
// which patches the Teensy core `usb_desc.h` file during compilation.
//
// Pair this with a polling rate checker like HamsterWheelMouseTester:
// https://github.com/szabodanika/HamsterWheel

// Initialize the display object using the screen dimensions from config.h
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool display_present = false; // Flag to track if the display was found

// --- Configuration ---
// The radius of the circular motion for the mouse cursor.
const int CIRCLE_RADIUS = 100;
// The change in angle per loop. A smaller value gives a smoother circle.
const float ANGLE_STEP = 0.08f;

// --- Global variables ---
float angle = 0.0f;
int last_x = 0;
int last_y = 0;

// Helper function to draw the initial screen on the OLED.
void drawStartupScreen() {
  // Only proceed if the display is actually connected.
  if (!display_present) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // --- Title ---
  display.setTextSize(1);
  display.setCursor((SCREEN_WIDTH - 14 * 6) / 2, 0);
  display.println(F("Polling Tester"));
  display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);

  // --- Instruction ---
  display.setTextSize(1);
  display.setCursor(32, 28); 
  display.println(F("Hold to Test"));
  
  // --- Footer ---
  display.setCursor(20, 56);
  display.println(F(GITHUB_TAG));

  display.display();
}

void setup() {
  // Set the button pin to input with an internal pull-up resistor and LED.
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Allow time for the USB host (PC) to enumerate and configure the Teensy.
  delay(2000);

  // Initialize I2C communication and check for the OLED display.
  display_present = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);

  // Check the low-level usb_configuration variable. (PC connection status)
  if (usb_configuration == 0) {
    // If not connected to a PC halt and flash LED.
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(400);
    }
  }

  // Draw the initial informational screen, if a display is available.
  drawStartupScreen();

  // Mouse.begin() is still necessary to configure the USB stack for HID Mouse.
  // The actual polling rate is determined by the descriptor, not this function.
  Mouse.begin();
}

void loop() {
  // Check the state of the button. The logic runs only when the button is pressed (LOW).
  if (digitalRead(PIN_BUTTON) == LOW) {
    // Calculate the new desired cursor position on a circle.
    // Using floating-point math provides a smooth path.
    int current_x = (int)(CIRCLE_RADIUS * cos(angle));
    int current_y = (int)(CIRCLE_RADIUS * sin(angle));

    // Calculate the delta (change) from the last position.
    // The mouse sends relative movements, not absolute coordinates.
    int dx = current_x - last_x;
    int dy = current_y - last_y;

    // We call usb_mouse_move() on EVERY loop iteration, without checking if
    // movement occurred. This is the key to achieving a true 8kHz polling rate.
    // If dx and dy are both 0 due to integer truncation, a (0,0) packet is sent.
    // The host receives this packet, confirming the 125us polling interval,
    // but simply doesn't move the cursor. This prevents skipped updates which
    // would otherwise lower the effective measured polling rate.
    usb_mouse_move(dx, dy, 0, 0);

    // Store the current position to calculate the next delta.
    last_x = current_x;
    last_y = current_y;

    // Increment the angle for the next point on the circle.
    angle += ANGLE_STEP;

    // Wrap the angle around if it completes a full circle to prevent it
    // from growing indefinitely and losing precision.
    if (angle > TWO_PI) {
      angle -= TWO_PI;
    }
  }
}