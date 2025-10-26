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

// --- State Machine ---
enum class State {
    SETUP,
    SELECT_MENU,
    AUTO_MODE,
    MANUAL_MODE,
    ERROR_HALT
};
State currentState = State::SETUP;

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
void drawOperationScreen();
void enterErrorState(const char* errorMessage);
void measureLatency();

// --- Setup Function ---
void setup() {
    pinMode(PIN_LED_BUILTIN, OUTPUT);
    digitalWrite(PIN_LED_BUILTIN, LOW);

    pinMode(PIN_SEND_CLICK, OUTPUT);
    digitalWrite(PIN_SEND_CLICK, LOW); // Ensure click pin is off initially
    pinMode(PIN_RECEIVE_CLICK, INPUT);

    debouncer.attach(PIN_BUTTON, INPUT_PULLUP);
    debouncer.interval(25); // Debounce interval in ms

    // Initialize display
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        // This is a critical error, but we can't display it.
        // We'll enter the error state and hope the LED is enough.
        enterErrorState("Monitor Fail");
        return; // Halt setup
    }
    display.clearDisplay();
    display.display();

    // --- Component Checks ---
    bool monitorOk = true; // If we're here, monitor is working

    // Check light sensor
    int initialLightReading = analogRead(PIN_LIGHT_SENSOR);
    bool sensorOk = (initialLightReading < LIGHT_SENSOR_ERROR_MAX);
    if (!sensorOk) {
        drawSetupScreen(monitorOk, sensorOk, false);
        enterErrorState("Sensor Error");
        return;
    }

    // Check for mouse presence (simple voltage read)
    // analogRead returns 0-1023 for 0-3.3V.
    float voltage = analogRead(PIN_RECEIVE_CLICK) * (3.3 / 1023.0);
    bool mouseOk = (voltage > MOUSE_PRESENCE_VOLTAGE);
    if (!mouseOk) {
        drawSetupScreen(monitorOk, sensorOk, mouseOk);
        enterErrorState("Mouse Error");
        return;
    }
    
    // All checks passed
    drawSetupScreen(monitorOk, sensorOk, mouseOk);

    // Wait for user to continue
    elapsedMillis holdTimer;
    while (true) {
        debouncer.update();
        if (debouncer.read() == LOW) { 
            if (holdTimer > BUTTON_HOLD_DURATION_MS) {
                currentState = State::SELECT_MENU;
                break; // Exit setup loop
            }
        } else {
            holdTimer = 0; // Reset timer if button is released
        }
    }
}

// --- Main Loop ---
void loop() {
    debouncer.update();

    switch (currentState) {
        case State::SETUP:
            // Should not be in this state during loop
            break;
        case State::SELECT_MENU:
            if (debouncer.fell()) { // Short click
                menuSelection = (menuSelection + 1) % menuOptionCount;
            }
            if (debouncer.read() == LOW && debouncer.duration() > BUTTON_HOLD_DURATION_MS) { // Long hold
                if (menuSelection == 0) {
                    currentState = State::AUTO_MODE;
                } else {
                    currentState = State::MANUAL_MODE;
                }
                // Clear stats for the new session
                stats = LatencyStats(); 
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
            if (digitalReadFast(PIN_RECEIVE_CLICK) == HIGH) {
                measureLatency();
            }
            break;
        case State::ERROR_HALT:
            // Do nothing, program is halted
            return;
    }
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
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    switch (currentState) {
        case State::SELECT_MENU:
            drawMenuScreen();
            break;
        case State::AUTO_MODE:
        case State::MANUAL_MODE:
            drawOperationScreen();
            break;
        default:
            // Do not clear display in setup or error state from here
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
        display.setCursor(10, 50);
        display.println("Hold Button to Continue");
    }

    display.setCursor(20, 56);
    display.setTextSize(1);
    display.println(GITHUB_TAG);
    display.display();
}

void drawMenuScreen() {
    display.setCursor(30, 0);
    display.println("Select Mode");

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
    display.print("L:");
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
    digitalWrite(PIN_LED_BUILTIN, HIGH); // Turn on LED to indicate error
    // The display will be left as it was, showing the error message from setup
}