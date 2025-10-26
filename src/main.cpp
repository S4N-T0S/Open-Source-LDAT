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
    UE4_APERTURE,
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
LatencyStats statsAuto;   // Stats for the standard Automatic mode
LatencyStats statsBtoW;   // Stats for UE4 Black-to-White
LatencyStats statsWtoB;   // Stats for UE4 White-to-Black

// --- UE4 Mode State ---
// This tracks whether the next measurement should be Black-to-White or White-to-Black
bool ue4_isWaitingForWhite = true;

// --- Forward Declarations ---
void updateDisplay();
void drawSetupScreen(bool monitorOk, bool sensorOk, bool mouseOk);
void drawMenuScreen();
void drawHoldActionScreen();
void drawOperationScreen();
void drawAutoModeStats();
void drawUe4ModeStats();
void drawMouseDebugScreen();
void drawLightSensorDebugScreen();
void enterErrorState(const char* errorMessage);
unsigned long measureLatency(bool waitForHigh); // Returns latency in micros or 0 on timeout
void updateStats(LatencyStats& stats, unsigned long latencyMicros); // Helper to calculate stats

// --- Setup Function ---
void setup() {
    pinMode(PIN_LED_BUILTIN, OUTPUT);
    digitalWrite(PIN_LED_BUILTIN, LOW);

    // Using Teensy-specific digitalWriteFast for maximum speed.
    pinMode(PIN_SEND_CLICK, OUTPUT);
    digitalWriteFast(PIN_SEND_CLICK, LOW); // Ensure click pin is off initially

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

    // Check light sensor for stability.
    int minLightReading = 1023;
    int maxLightReading = 0;
    elapsedMillis componentCheckTimer;
    while (componentCheckTimer < FLUC_CHECK_DURATION_MS) {
        int currentReading = analogRead(PIN_LIGHT_SENSOR);
        if (currentReading < minLightReading) minLightReading = currentReading;
        if (currentReading > maxLightReading) maxLightReading = currentReading;
        delay(10); // Briefly pause to not overwhelm the ADC
    }
    bool sensorOk = (maxLightReading - minLightReading) < SENSOR_FLUCTUATION_THRESHOLD;

    // Check for mouse presence by measuring the stability (fluctuation) on the analog pin.
    int minMouseReading = 1023;
    int maxMouseReading = 0;
    componentCheckTimer = 0; // Reset the timer for the next check
    while (componentCheckTimer < FLUC_CHECK_DURATION_MS) {
        int currentReading = analogRead(PIN_MOUSE_PRESENCE);
        if (currentReading < minMouseReading) minMouseReading = currentReading;
        if (currentReading > maxMouseReading) maxMouseReading = currentReading;
        delay(10); // Briefly pause to not overwhelm the ADC
    }
    bool mouseOk = (maxMouseReading - minMouseReading) < MOUSE_FLUCTUATION_THRESHOLD;
    
    // Display the setup screen once, so user sees the status
    drawSetupScreen(monitorOk, sensorOk, mouseOk);

    // Now handle errors, directing to debug screens if necessary
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

    // Global check to enter the HOLD_ACTION state from any operational state
    bool canEnterHold = (currentState == State::SELECT_MENU || currentState == State::AUTO_MODE || 
                         currentState == State::UE4_APERTURE || currentState == State::DEBUG_MOUSE || 
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
                // A long press is handled by the HOLD_ACTION state, so this only fires for short presses.
                menuSelection = (menuSelection + 1) % menuOptionCount;
            }
            break;
        case State::HOLD_ACTION:
            // Logic is entirely on release to prevent race conditions and improve UX.
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
                        statsAuto = LatencyStats(); // Clear stats for the new session
                    } else {
                        currentState = State::UE4_APERTURE;
                        ue4_isWaitingForWhite = false; 
                        statsBtoW = LatencyStats();   // Clear stats for the new session
                        statsWtoB = LatencyStats();
                    }
                }
                // Action 3: Aborted hold, return to previous state
                else {
                    currentState = previousState;
                }
            }
            break;
        case State::AUTO_MODE: {
            // Send click
            digitalWriteFast(PIN_SEND_CLICK, HIGH);
            delayMicroseconds(500); // Hold the click for a reliable duration
            digitalWriteFast(PIN_SEND_CLICK, LOW);
            
            // Measure the latency. measureLatency() returns 0 on timeout.
            unsigned long latencyMicros = measureLatency(true); // Measure rising edge
            
            // Only update stats if the measurement was successful
            if (latencyMicros > 0) {
                updateStats(statsAuto, latencyMicros);
            }

            delay(AUTO_MODE_CLICK_DELAY_MS); // Wait before next run
            break;
        }
        case State::UE4_APERTURE: {
            // Send click - this triggers both white->black and black->white transitions
            digitalWriteFast(PIN_SEND_CLICK, HIGH);
            delayMicroseconds(500);
            digitalWriteFast(PIN_SEND_CLICK, LOW);
            
            unsigned long latencyMicros = 0;
            
            // Measure the appropriate transition based on the current sub-state
            if (ue4_isWaitingForWhite) {
                latencyMicros = measureLatency(true); // Measure black-to-white
                if (latencyMicros > 0) {
                    // SUCCESS: Update stats and flip state for the next run
                    updateStats(statsBtoW, latencyMicros);
                    ue4_isWaitingForWhite = false; // Now we expect the screen to go black
                }
            } else {
                latencyMicros = measureLatency(false); // Measure white-to-black
                if (latencyMicros > 0) {
                    // SUCCESS: Update stats and flip state for the next run
                    updateStats(statsWtoB, latencyMicros);
                    ue4_isWaitingForWhite = true; // Now we expect the screen to go white
                }
            }
            // If latencyMicros is 0 (timeout), the state is NOT flipped.
            // This forces the system to re-attempt the same measurement, preventing desync.

            delay(AUTO_MODE_CLICK_DELAY_MS); // Wait before next run
            break;
        }
        case State::ERROR_HALT:
            return; // Halt program for critical errors
    }
    // Update the display in every loop cycle for all active states
    updateDisplay();
}

// --- Core Latency Measurement ---
// This function blocks execution and measures the time until the light sensor crosses a threshold.
// It returns the latency in microseconds on success, or 0 on timeout.
unsigned long measureLatency(bool waitForHigh) {
    elapsedMicros timer;
    
    // The measurement loop is the most performance-critical part of the code.
    // It uses direct hardware reads and avoids any complex logic or library calls
    // to ensure the lowest possible overhead.

    if (waitForHigh) {
        // Wait for the light sensor to cross the HIGH (white) threshold
        while (analogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
            if (timer > 5000000) return 0; // 5 second timeout
        }
    } else {
        // Wait for the light sensor to drop below the LOW (black) threshold
        while (analogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD) {
            if (timer > 5000000) return 0; // 5 second timeout
        }
    }
    
    return timer; // Success
}

// --- Helper function to centralize statistics calculations ---
void updateStats(LatencyStats& stats, unsigned long latencyMicros) {
    float latencyMillis = latencyMicros / 1000.0f;

    // Update the provided stats struct
    stats.runCount++;
    stats.lastLatency = latencyMillis;
    // Use a numerically stable rolling average formula
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
        case State::UE4_APERTURE:
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

    display.setCursor(10, 10);
    display.print("Monitor: ");
    display.println(monitorOk ? "Working" : "Not Detected");

    display.setCursor(10, 20);
    display.print("Sensor: ");
    display.println(sensorOk ? "Stable" : "Unstable");

    display.setCursor(10, 30);
    display.print("Mouse: ");
    display.println(mouseOk ? "Detected" : "Not Detected");

    if (monitorOk && sensorOk && mouseOk) {
        display.drawLine(0, 38, SCREEN_WIDTH-1, 38, SSD1306_WHITE);
        display.setCursor(20, 42);
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
    int rawValue = analogRead(PIN_MOUSE_PRESENCE);
    
    display.setCursor(0, 16);
    display.print("Pin: ");
    display.print(PIN_MOUSE_PRESENCE);

    display.setCursor(0, 28);
    display.print("Live Reading: ");
    display.print(rawValue);

    display.setCursor(0, 40);
    display.print("Fails if Fluct > ");
    display.print(MOUSE_FLUCTUATION_THRESHOLD);

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
    display.print("Fails if Fluct > ");
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
        if (i == 1) display.println("UE4 Aperture");
    }
    
    display.setCursor(20, 56);
    display.println(GITHUB_TAG);
}

// --- Main Operation Screen Router ---
void drawOperationScreen() {
    // Call the correct display function based on the current mode
    if (currentState == State::AUTO_MODE) {
        drawAutoModeStats();
    } else if (currentState == State::UE4_APERTURE) {
        drawUe4ModeStats();
    }
}

// --- Specific Stat Screens ---
void drawAutoModeStats() {
    char buf[16];

    // Mode
    display.setCursor(88, 0);
    display.print("AUTO");

    // Light Sensor Reading
    display.setCursor(0, 0);
    display.print("Light:");
    display.print(analogRead(PIN_LIGHT_SENSOR));

    // Last Latency
    dtostrf(statsAuto.lastLatency, 5, 2, buf);
    display.setCursor(0, 15);
    display.print("Last: ");
    display.print(buf);
    display.print("ms");

    // Run Count
    display.setCursor(80, 15);
    display.print("N: ");
    display.print(statsAuto.runCount);

    // Average Latency
    dtostrf(statsAuto.avgLatency, 5, 2, buf);
    display.setCursor(0, 30);
    display.print("Avg:  ");
    display.print(buf);
    display.print("ms");
    
    // Min/Max
    display.setTextSize(1);
    dtostrf(statsAuto.minLatency, 4, 1, buf);
    display.setCursor(0, 45);
    display.print("Min:");
    display.print(buf);

    dtostrf(statsAuto.maxLatency, 4, 1, buf);
    display.setCursor(64, 45);
    display.print("Max:");
    display.print(buf);

    // Footer
    display.setCursor(20, 56);
    display.println(GITHUB_TAG);
}

void drawUe4ModeStats() {
    char buf[10];

    // --- Title ---
    display.setCursor(15, 0);
    display.print("UE4 Aperture Grille");

    // --- Column Headers ---
    display.setCursor(0, 12);
    display.print("B-to-W");
    display.setCursor(74, 12);
    display.print("W-to-B");
    display.drawLine(64, 10, 64, 52, SSD1306_WHITE); // Vertical divider

    // --- Last Latency ---
    dtostrf(statsBtoW.lastLatency, 5, 2, buf);
    display.setCursor(0, 22);
    display.print("L:"); display.print(buf);
    
    dtostrf(statsWtoB.lastLatency, 5, 2, buf);
    display.setCursor(68, 22);
    display.print("L:"); display.print(buf);
    
    // --- Average Latency ---
    dtostrf(statsBtoW.avgLatency, 5, 2, buf);
    display.setCursor(0, 32);
    display.print("A:"); display.print(buf);

    dtostrf(statsWtoB.avgLatency, 5, 2, buf);
    display.setCursor(68, 32);
    display.print("A:"); display.print(buf);

    // --- Min/Max Latency ---
    dtostrf(statsBtoW.minLatency, 4, 1, buf);
    display.setCursor(0, 42);
    display.print("m:"); display.print(buf);

    dtostrf(statsWtoB.minLatency, 4, 1, buf);
    display.setCursor(68, 42);
    display.print("m:"); display.print(buf);
    
    dtostrf(statsBtoW.maxLatency, 4, 1, buf);
    display.setCursor(0, 52);
    display.print("M:"); display.print(buf);
    
    dtostrf(statsWtoB.maxLatency, 4, 1, buf);
    display.setCursor(68, 52);
    display.print("M:"); display.print(buf);
    
    // --- Footer Run Count ---
    display.setCursor(20, 56);
    display.print("N: ");
    display.print(statsBtoW.runCount); // or statsWtoB.runCount, they are the same
}


// --- Error Handling ---
void enterErrorState(const char* errorMessage) {
    currentState = State::ERROR_HALT;
    digitalWrite(PIN_LED_BUILTIN, HIGH); // Turn on solid LED to indicate a critical error
    // The display will be left as it was, or will be blank if it failed to init.
}