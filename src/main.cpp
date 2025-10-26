#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Bounce2.h>
#include <elapsedMillis.h>
#include "../include/config.h"

// --- Global Objects ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Bounce debouncer = Bounce();
elapsedMillis ledTimer; // For blinking LED in debug modes

// --- State Machine ---
enum class State {
    SETUP,
    SELECT_MENU,
    HOLD_ACTION,
    AUTO_MODE,
    MANUAL_MODE,
    ERROR_HALT,
    DEBUG_MOUSE,
    DEBUG_LSENSOR
};
State currentState = State::SETUP;
State previousState = State::SETUP;

// --- Menu Variables ---
int menuSelection = 0;
const int menuOptionCount = 2;

// --- Statistics ---
struct LatencyStats {
    unsigned long runCount = 0;
    float lastLatency = 0.0;
    float avgLatency = 0.0;
    float minLatency = 99999.0;
    float maxLatency = 0.0;
};
LatencyStats stats;

// --- Forward Declarations ---
void updateDisplay();
void drawSetupScreen(bool monitorOk, bool sensorOk, bool mouseOk);
void drawMenuScreen();
void drawHoldActionScreen();
void drawOperationScreen();
void drawMouseDebugScreen();
void drawLightSensorDebugScreen();
void enterErrorState(const char* errorMessage);
void measureLatency();

// --- Setup Function ---
void setup() {
    pinMode(PIN_LED_BUILTIN, OUTPUT);
    digitalWrite(PIN_LED_BUILTIN, LOW);

    pinMode(PIN_SEND_CLICK, OUTPUT);
    digitalWriteFast(PIN_SEND_CLICK, LOW); // Ensure click pin is off initially
    pinMode(PIN_RECEIVE_CLICK, INPUT);

    debouncer.attach(PIN_BUTTON, INPUT_PULLUP);
    debouncer.interval(25); // Debounce interval in ms

    // Initialize display
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        // This is a critical error, but we can't display it.
        // We'll enter the error state which turns on the solid LED.
        enterErrorState("Monitor Fail");
        return; // Halt setup
    }
    display.clearDisplay();
    display.display();

    // --- Component Checks ---
    bool monitorOk = true; // If we're here, monitor is working

    // Check light sensor for stability and range
    int minLightReading = 1023;
    int maxLightReading = 0;
    elapsedMillis sensorCheckTimer;
    while (sensorCheckTimer < SENSOR_CHECK_DURATION_MS) {
        int currentReading = analogRead(PIN_LIGHT_SENSOR);
        if (currentReading < minLightReading) minLightReading = currentReading;
        if (currentReading > maxLightReading) maxLightReading = currentReading;
        delay(10); // Briefly pause to not overwhelm the ADC
    }
    bool sensorIsStable = (maxLightReading - minLightReading) < SENSOR_FLUCTUATION_THRESHOLD;
    bool sensorIsInRange = (maxLightReading < LIGHT_SENSOR_ERROR_MAX);
    bool sensorOk = sensorIsStable && sensorIsInRange;

    // Check for mouse presence (simple voltage read)
    // analogRead returns 0-1023 for 0-3.3V.
    float voltage = analogRead(PIN_RECEIVE_CLICK) * (3.3 / 1023.0);
    bool mouseOk = (voltage > MOUSE_PRESENCE_VOLTAGE);
    
    // Display the setup screen once, so user sees the status
    drawSetupScreen(monitorOk, sensorOk, mouseOk);

    // Now handle errors, directing to debug screens
    if (!sensorOk) {
        currentState = State::DEBUG_LSENSOR;
        digitalWrite(PIN_LED_BUILTIN, HIGH); // Turn on LED to start blink cycle
        return; // Skip the rest of setup and go to loop()
    }

    if (!mouseOk) {
        currentState = State::DEBUG_MOUSE;
        digitalWrite(PIN_LED_BUILTIN, HIGH); // Turn on LED to start blink cycle
        return; // Skip the rest of setup and go to loop()
    }
    
    // All checks passed, wait for user to continue.
    while (true) {
        debouncer.update();

        // If the button is currently being held down, show the progress bars
        if (debouncer.read() == LOW) {
            if (debouncer.duration() > BUTTON_HOLD_START_MS) {
                drawHoldActionScreen();
                display.display();
            }
        } 
        // If the button is NOT being held down...
        else {
            // Check if it was just released.
            if (debouncer.rose()) {
                unsigned long heldDuration = debouncer.previousDuration();

                // Priority 1: Check if it was held long enough for a RESET.
                if (heldDuration > BUTTON_RESET_DURATION_MS) {
                    SCB_AIRCR = 0x05FA0004; // Software Reset
                    while(true); // Will not be reached, but good practice
                }
                
                // Priority 2: Check if it was held long enough to SELECT.
                else if (heldDuration > BUTTON_HOLD_DURATION_MS) {
                    currentState = State::SELECT_MENU;
                    break; // Exit the setup loop and proceed to the main program loop.
                }
                // Otherwise, it was a short press or an aborted hold, do nothing.
            }
            
            // While waiting for a valid hold/release, keep showing the screen.
            drawSetupScreen(monitorOk, sensorOk, mouseOk);
        }
    }
}

// --- Main Loop ---
void loop() {
    // Always update the debouncer for any state that might use the button
    debouncer.update();

    // Global check to enter the HOLD_ACTION state from operational states
    bool canEnterHold = (currentState == State::SELECT_MENU || currentState == State::AUTO_MODE || 
                         currentState == State::MANUAL_MODE || currentState == State::DEBUG_MOUSE || 
                         currentState == State::DEBUG_LSENSOR);
    if (canEnterHold && debouncer.read() == LOW && debouncer.duration() > BUTTON_HOLD_START_MS) {
        previousState = currentState; // Remember where we came from
        currentState = State::HOLD_ACTION;
    }

    // Handle blinking LED for debug states
    if (currentState == State::DEBUG_MOUSE || currentState == State::DEBUG_LSENSOR) {
        if (ledTimer > 1000) { // 1 second interval
            digitalWrite(PIN_LED_BUILTIN, !digitalRead(PIN_LED_BUILTIN)); // Toggle LED
            ledTimer = 0; // Reset timer
        }
    }

    switch (currentState) {
        case State::SETUP:
            // Should not be in this state during loop, setup handles its own loop
            break;
        case State::DEBUG_MOUSE:
        case State::DEBUG_LSENSOR:
            // No action logic needed here, just display updates and global reset check
            break; 
        case State::SELECT_MENU:
            // Act on button release (short press) to cycle menu
            if (debouncer.rose()) { 
                // A long press is now handled by the HOLD_ACTION state, so this only fires for short presses.
                menuSelection = (menuSelection + 1) % menuOptionCount;
            }
            break;
        case State::HOLD_ACTION:
            // Logic is now entirely on release to prevent race conditions and improve UX.
            if (debouncer.rose()) {
                unsigned long heldDuration = debouncer.previousDuration();
                // Action 1: RESET (highest priority)
                if (heldDuration > BUTTON_RESET_DURATION_MS) {
                    SCB_AIRCR = 0x05FA0004; // Software Reset
                    while(true);
                } 
                // Action 2: SELECT
                else if (heldDuration > BUTTON_HOLD_DURATION_MS && previousState == State::SELECT_MENU) {
                    if (menuSelection == 0) {
                        currentState = State::AUTO_MODE;
                    } else {
                        currentState = State::MANUAL_MODE;
                    }
                    stats = LatencyStats(); // Clear stats for the new session
                }
                // Action 3: Aborted hold, return to previous state
                else {
                    currentState = previousState;
                }
            }
            break;
        case State::AUTO_MODE:
            // Send click
            digitalWriteFast(PIN_SEND_CLICK, HIGH);
            delayMicroseconds(100); // Hold the click for a very short duration
            digitalWriteFast(PIN_SEND_CLICK, LOW);
            
            measureLatency();
            delay(AUTO_MODE_CLICK_DELAY_MS); // Wait before next run
            break;
        case State::MANUAL_MODE:
            // Check for a click from the mouse
            if (digitalReadFast(PIN_RECEIVE_CLICK) == HIGH) {
                measureLatency();
            }
            break;
        case State::ERROR_HALT:
            return; // Halt program for critical errors
    }
    // Update the display in every loop cycle for all active states
    updateDisplay();
}

// --- Core Latency Measurement ---
void measureLatency() {
    elapsedMicros timer;
    
    // Wait for the light sensor to cross the threshold
    // Add a timeout to prevent getting stuck
    while (analogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
        if (timer > 5000000) return; // 5 second timeout
    }
    
    unsigned long latencyMicros = timer;
    float latencyMillis = latencyMicros / 1000.0f;

    // Update stats
    stats.runCount++;
    stats.lastLatency = latencyMillis;
    stats.avgLatency = stats.avgLatency + (latencyMillis - stats.avgLatency) / stats.runCount;
    if (latencyMillis < stats.minLatency) stats.minLatency = latencyMillis;
    if (latencyMillis > stats.maxLatency) stats.maxLatency = latencyMillis;
}

// --- Display Functions ---
void updateDisplay() {
    // Do not update the display from here if we are in setup mode, as it has its own display logic
    if (currentState == State::SETUP) return;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    switch (currentState) {
        case State::SELECT_MENU:
            drawMenuScreen();
            break;
        case State::HOLD_ACTION:
            drawHoldActionScreen();
            break;
        case State::AUTO_MODE:
        case State::MANUAL_MODE:
            drawOperationScreen();
            break;
        case State::DEBUG_MOUSE:
            drawMouseDebugScreen();
            break;
        case State::DEBUG_LSENSOR:
            drawLightSensorDebugScreen();
            break;
        default:
            // Do not clear display in error state from here
            break;
    }
    display.display();
}

void drawSetupScreen(bool monitorOk, bool sensorOk, bool mouseOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(35, 0);
    display.println("SETUP MODE");
    display.drawLine(0, 8, SCREEN_WIDTH-1, 8, SSD1306_WHITE);

    display.setCursor(10, 16);
    display.print("Monitor: ");
    display.println(monitorOk ? "Working" : "Not Detected");

    display.setCursor(10, 26);
    display.print("Sensor: ");
    display.println(sensorOk ? "Working" : "Error");

    display.setCursor(10, 36);
    display.print("Mouse: ");
    display.println(mouseOk ? "Detected" : "Not Detected");

    if (monitorOk && sensorOk && mouseOk) {
        display.setCursor(20, 46);
        display.println("Hold Button Now");
    }

    display.setCursor(20, 56);
    display.setTextSize(1);
    display.println(GITHUB_TAG);
    display.display();
}

// Hold Action Screen with Progress Bars
void drawHoldActionScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 0);
    display.println("Hold for action...");

    unsigned long holdTime = debouncer.duration();
    
    // --- SELECT Bar ---
    display.setCursor(0, 18);
    // Only show "SELECT" as an option if it's a valid action from the current state
    if (previousState == State::SELECT_MENU || currentState == State::SETUP) {
        display.print("SELECT");
    }
    float selectProgress = (float)(holdTime - BUTTON_HOLD_START_MS) / (BUTTON_HOLD_DURATION_MS - BUTTON_HOLD_START_MS);
    selectProgress = constrain(selectProgress, 0.0, 1.0);
    int barWidth = 70;
    int barX = 55;
    display.drawRect(barX, 16, barWidth, 10, SSD1306_WHITE);
    display.fillRect(barX, 16, barWidth * selectProgress, 10, SSD1306_WHITE);

    // --- RESET Bar ---
    display.setCursor(0, 38);
    display.print("RESET");
    float resetProgress = (float)(holdTime - BUTTON_HOLD_START_MS) / (BUTTON_RESET_DURATION_MS - BUTTON_HOLD_START_MS);
    resetProgress = constrain(resetProgress, 0.0, 1.0);
    display.drawRect(barX, 36, barWidth, 10, SSD1306_WHITE);
    display.fillRect(barX, 36, barWidth * resetProgress, 10, SSD1306_WHITE);
}


void drawMouseDebugScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Title (11 chars * 6 pixels/char = 66 pixels. (128 - 66) / 2 = 31)
    display.setCursor(31, 0);
    display.println("MOUSE DEBUG");
    display.drawLine(0, 8, SCREEN_WIDTH-1, 8, SSD1306_WHITE);

    // Live Data
    int rawValue = analogRead(PIN_RECEIVE_CLICK);
    float voltage = rawValue * (3.3 / 1023.0);
    char buf[10];

    display.setCursor(0, 16);
    display.print("Pin: ");
    display.print(PIN_RECEIVE_CLICK);

    display.setCursor(0, 28);
    display.print("Raw Reading: ");
    display.print(rawValue);

    dtostrf(voltage, 4, 2, buf);
    display.setCursor(0, 40);
    display.print("Voltage: ");
    display.print(buf);
    display.print("V");

    // Footer
    display.setCursor(20, 56);
    display.println(GITHUB_TAG);
}

void drawLightSensorDebugScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Title (13 chars * 6 pixels/char = 78 pixels. (128 - 78) / 2 = 25)
    display.setCursor(25, 0);
    display.println("LSENSOR DEBUG");
    display.drawLine(0, 8, SCREEN_WIDTH-1, 8, SSD1306_WHITE);

    // Live Data
    int rawValue = analogRead(PIN_LIGHT_SENSOR);

    display.setCursor(0, 16);
    display.print("Pin: ");
    display.print(PIN_LIGHT_SENSOR);

    display.setCursor(0, 28);
    display.print("Live Reading: ");
    display.print(rawValue);

    display.setCursor(0, 40);
    display.print("IF >");
    display.print(LIGHT_SENSOR_ERROR_MAX);
    display.print(" or Fluc >");
    display.print(SENSOR_FLUCTUATION_THRESHOLD);


    // Footer
    display.setCursor(20, 56);
    display.println(GITHUB_TAG);
}

void drawMenuScreen() {
    display.setCursor(30, 0);
    display.println("Select Mode");
    display.drawLine(0, 8, SCREEN_WIDTH-1, 8, SSD1306_WHITE);

    for (int i = 0; i < menuOptionCount; ++i) {
        display.setCursor(10, 16 + i * 12);
        if (i == menuSelection) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        if (i == 0) display.println("Automatic");
        if (i == 1) display.println("Manual");
    }
    
    display.setCursor(20, 56);
    display.println(GITHUB_TAG);
}

void drawOperationScreen() {
    char buf[16];

    // Mode
    display.setCursor(88, 0);
    display.print(currentState == State::AUTO_MODE ? "AUTO" : "MANUAL");

    // Light Sensor Reading
    display.setCursor(0, 0);
    display.print("LightSen:");
    display.print(analogRead(PIN_LIGHT_SENSOR));

    // Last Latency
    dtostrf(stats.lastLatency, 5, 2, buf);
    display.setCursor(0, 15);
    display.print("Last: ");
    display.print(buf);
    display.print("ms");

    // Run Count
    display.setCursor(80, 15);
    display.print("N: ");
    display.print(stats.runCount);

    // Average Latency
    dtostrf(stats.avgLatency, 5, 2, buf);
    display.setCursor(0, 30);
    display.print("Avg:  ");
    display.print(buf);
    display.print("ms");
    
    // Min/Max
    display.setTextSize(1);
    dtostrf(stats.minLatency, 4, 1, buf);
    display.setCursor(0, 45);
    display.print("Min:");
    display.print(buf);

    dtostrf(stats.maxLatency, 4, 1, buf);
    display.setCursor(64, 45);
    display.print("Max:");
    display.print(buf);

    // Footer
    display.setCursor(20, 56);
    display.println(GITHUB_TAG);
}

// --- Error Handling ---
void enterErrorState(const char* errorMessage) {
    currentState = State::ERROR_HALT;
    digitalWrite(PIN_LED_BUILTIN, HIGH); // Turn on solid LED to indicate a critical error
    // The display will be left as it was, or will be blank if it failed to init.
}