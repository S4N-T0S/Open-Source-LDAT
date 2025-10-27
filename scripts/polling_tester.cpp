#include <Arduino.h>

// This sketch uses a lower-level USB function to ensure maximum speed.
// It is designed to test the 8kHz polling rate capability of the Teensy 4.1.
// The polling rate is set to 8kHz (125us interval) via the `8kHz_polling.py`
// which patches the Teensy core `usb_desc.h` file during compilation.
//
// Pair this with a polling rate checker like HamsterWheelMouseTester:
// https://github.com/szabodanika/HamsterWheel

// --- Configuration ---
// The radius of the circular motion for the mouse cursor.
const int CIRCLE_RADIUS = 100;
// The change in angle per loop. A smaller value gives a smoother circle.
const float ANGLE_STEP = 0.08f;

// --- Global variables ---
float angle = 0.0f;
int last_x = 0;
int last_y = 0;

void setup() {
  // Mouse.begin() is still necessary to configure the USB stack for HID Mouse.
  // The actual polling rate is determined by the descriptor, not this function.
  Mouse.begin();
}

void loop() {
  // Calculate the new desired cursor position on a circle.
  // Using floating-point math provides a smooth path.
  int current_x = (int)(CIRCLE_RADIUS * cos(angle));
  int current_y = (int)(CIRCLE_RADIUS * sin(angle));

  // Calculate the delta (change) from the last position.
  // The mouse sends relative movements, not absolute coordinates.
  int dx = current_x - last_x;
  int dy = current_y - last_y;

  // --- CRITICAL FIX ---
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