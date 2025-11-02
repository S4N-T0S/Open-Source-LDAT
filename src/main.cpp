/*
 *  Open-Source-LDAT - Latency Detection and Analysis Tool
 *  Copyright (C) 2025 S4N-T0S
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Bounce2.h>
#include <elapsedMillis.h>
#include <Entropy.h>
#include <ADC.h> // Teensy-specific ADC library for high-speed analog reads
#include <SD.h>
#include <vector>
#include "../include/config.h"

// --- Global Objects ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Bounce debouncer = Bounce();
elapsedMillis ledTimer; // For blinking LED in debug modes
ADC *adc = new ADC(); // ADC object for optimized analog reads

// --- State Machine ---
enum class State {
    SETUP,
    SELECT_MENU,
    SELECT_RUN_LIMIT,
    SELECT_DEBUG_MENU,
    HOLD_ACTION,
    AUTO_MODE,
    AUTO_UE4_APERTURE,
    DIRECT_UE4_APERTURE,
    RUNS_COMPLETE,
    ERROR_HALT,
    DEBUG_MOUSE,
    DEBUG_LSENSOR,
    DEBUG_POLLING_TEST
};
State currentState = State::SETUP;
State previousState = State::SETUP;
State selectedMode = State::SETUP;

// Enum for the result of the smart sync function
enum class SyncResult {
    SUCCESS,
    FAILED,
    HOLD_ABORT
};

// --- Menu Variables ---
int menuSelection = 0;
const int menuOptionCount = 3;
int runLimitMenuSelection = 0;
const int runLimitMenuOptionCount = 4;
int debugMenuSelection = 0;
const int debugMenuOptionCount = 3;
unsigned long maxRuns = 0;

// --- Statistics ---
struct LatencyStats {
    unsigned long runCount = 0;
    float lastLatency = 0.0;
    float avgLatency = 0.0;
    float minLatency = 999.0;
    float maxLatency = 0.0;
};
LatencyStats statsAuto;         // Stats for the standard Automatic mode
LatencyStats statsBtoW;         // Stats for Auto UE4 Black-to-White
LatencyStats statsWtoB;         // Stats for Auto UE4 White-to-Black
LatencyStats statsDirectBtoW;   // Stats for Direct UE4 Black-to-White
LatencyStats statsDirectWtoB;   // Stats for Direct UE4 White-to-Black

// --- Latency Data Storage for SD Logging ---
std::vector<float> latenciesAuto;
std::vector<float> latenciesBtoW;
std::vector<float> latenciesWtoB;
std::vector<float> latenciesDirectBtoW;
std::vector<float> latenciesDirectWtoB;

// --- UE4 Mode State ---
// This tracks whether the next measurement should be Black-to-White or White-to-Black
bool ue4_isWaitingForWhite = true;
bool isFirstUe4Run = true;
bool mouseIsOk = false;

// --- SD Card State ---
bool sdCardPresent = false;
bool dataHasBeenSaved = false;

// --- Polling Test Variables ---
const int CIRCLE_RADIUS = 100;
const float ANGLE_STEP = 0.08f;
float polltest_angle = 0.0f;
int polltest_last_x = 0;
int polltest_last_y = 0;


// --- Forward Declarations ---
void updateDisplay();
void drawSetupScreen(bool monitorOk, bool sensorOk, bool mouseOk, bool sdOk);
void drawMenuScreen();
void drawRunLimitMenuScreen();
void drawDebugMenuScreen();
void drawHoldActionScreen();
void drawOperationScreen();
void drawAutoModeStats();
void drawUe4StatsScreen(const char* title, const LatencyStats& b_to_w_stats, const LatencyStats& w_to_b_stats);
void drawMouseDebugScreen();
void drawLightSensorDebugScreen();
void drawPollingTestScreen();
void enterErrorState(const char* errorMessage);
void updateStats(LatencyStats& stats, std::vector<float>& latencies, unsigned long latencyMicros);
int fastAnalogRead(uint8_t pin);
void drawSyncScreen(const char* message, int y = 32);
SyncResult performSmartSync(bool isDirectMode);
void centerText(const char* text, int y = -1);
bool delayWithJitterAndAbortCheck(unsigned long baseDelayMs);
bool performMouseCheck();
void displayErrorScreen(const char* title, const char* line1, const char* line2, const char* line3, unsigned long delayMs = 3500);
void saveDataToSD(State mode, unsigned long run_limit, bool is_partial_save);

// --- Component Check Functions ---
bool performMouseCheck() {
    // Check for mouse presence by measuring the stability (fluctuation/voltage) on the analog pin.
    int minMouseReading = 1023;
    int maxMouseReading = 0;
    elapsedMillis componentCheckTimer;
    while (componentCheckTimer < FLUC_CHECK_DURATION_MS) {
        int currentReading = fastAnalogRead(PIN_MOUSE_PRESENCE);
        if (currentReading < minMouseReading) minMouseReading = currentReading;
        if (currentReading > maxMouseReading) maxMouseReading = currentReading;
        delay(10); // Briefly pause to not overwhelm the ADC
    }

    // Condition 1: Is the signal stable (low fluctuation)?
    bool isStable = (maxMouseReading - minMouseReading) < MOUSE_STABILITY_THRESHOLD_ADC;
    // Condition 2: Is the voltage level high enough?
    bool isHighEnough = minMouseReading > MOUSE_PRESENCE_MIN_ADC_VALUE;
    // The check passes only if BOTH conditions are true.
    return isStable && isHighEnough;
}

// --- Setup Function ---
void setup() {
    pinMode(PIN_LED_BUILTIN, OUTPUT);
    digitalWrite(PIN_LED_BUILTIN, LOW);

    // Seed the software pseudo-random generator with a true random number.
    randomSeed(Entropy.random());

    // Initialize USB Mouse functionality.
    Mouse.begin();

    // Set the click pin to output mode.
    pinMode(PIN_SEND_CLICK, OUTPUT);
    // Set the initial state to LOW using a fast method to ensure it's off before the loop begins.
    digitalWriteFast(PIN_SEND_CLICK, LOW);

    // Configure the mouse presence pin as an input with a pull-down resistor, prevents floating.
    pinMode(PIN_MOUSE_PRESENCE, INPUT_PULLDOWN);

    debouncer.attach(PIN_BUTTON, INPUT_PULLUP);
    debouncer.interval(25); // Debounce interval in ms

    // Initialize display
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        // This is a critical error, but we can't display it.
        // We'll enter the error state which turns on the solid LED.
        enterErrorState("Monitor Fail");
        return; // Halt setup
    }
    display.clearDisplay();
    display.display();

    // --- ADC Optimization ---
    // The following settings are applied to BOTH ADC controllers (ADC1 and ADC2)
    // automatically by the library, ensuring consistent high-speed, 8-bit operation.

    // Configure ADC1
    adc->adc0->setResolution(8);
    adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_HIGH_SPEED);
    adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::VERY_HIGH_SPEED);

    // Configure ADC2
    adc->adc1->setResolution(8);
    adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_HIGH_SPEED);
    adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::VERY_HIGH_SPEED);

    // --- SD Card Initialization ---
    if (ENABLE_SD_LOGGING) {
        if (SD.begin(BUILTIN_SDCARD)) {
            sdCardPresent = true;
            // Create the directory if it doesn't exist
            if (!SD.exists(SD_LOG_DIRECTORY)) {
                SD.mkdir(SD_LOG_DIRECTORY);
            }
        }
    }

    // --- Component Checks ---
    bool monitorOk = true; // If we're here, monitor is working

    // Check light sensor for stability.
    int minLightReading = 1023; // Start high to find the true minimum
    int maxLightReading = 0;    // Start low to find the true maximum
    elapsedMillis componentCheckTimer;
    while (componentCheckTimer < FLUC_CHECK_DURATION_MS) {
        int currentReading = fastAnalogRead(PIN_LIGHT_SENSOR);
        if (currentReading < minLightReading) minLightReading = currentReading;
        if (currentReading > maxLightReading) maxLightReading = currentReading;
        delay(10); // Briefly pause to not overwhelm the ADC
    }
    bool sensorOk = (maxLightReading - minLightReading) < SENSOR_FLUCTUATION_THRESHOLD;

    // Check for mouse presence using the new helper function.
    mouseIsOk = performMouseCheck();

    // Display the setup screen once, so user sees the status
    drawSetupScreen(monitorOk, sensorOk, mouseIsOk, sdCardPresent);

    // Now handle errors, directing to debug screens if necessary
    if (!sensorOk) {
        currentState = State::DEBUG_LSENSOR;
        digitalWrite(PIN_LED_BUILTIN, HIGH); // Turn on LED to start blink cycle
        return; // Skip the rest of setup and go to loop()
    }

    if (!mouseIsOk) {
        currentState = State::DEBUG_MOUSE;
        digitalWrite(PIN_LED_BUILTIN, HIGH); // Turn on LED to start blink cycle
        return; // Skip the rest of setup and go to loop()
    }

    // All checks passed, wait for user to continue.
    while (true) {
        debouncer.update();

        // If the button is currently being held down, show the progress bars
        if (debouncer.read() == LOW) {
            if (debouncer.currentDuration() > BUTTON_HOLD_START_MS) {
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

                // Priority 2: Check for DEBUG menu
                else if (heldDuration > BUTTON_DEBUG_DURATION_MS) {
                    debugMenuSelection = 0;
                    currentState = State::SELECT_DEBUG_MENU;
                    break;
                }

                // Priority 3: Check if it was held long enough to SELECT.
                else if (heldDuration > BUTTON_HOLD_DURATION_MS) {
                    currentState = State::SELECT_MENU;
                    break; // Exit the setup loop and proceed to the main program loop.
                }
                // Otherwise, it was a short press or an aborted hold, do nothing.
            }

            // While waiting for a valid hold/release, keep showing the screen.
            drawSetupScreen(monitorOk, sensorOk, mouseIsOk, sdCardPresent);
        }
    }
}

// --- Main Loop ---
void loop() {
    // Always update the debouncer for any state that might use the button
    debouncer.update();

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
            // Check for hold action to exit debug screens
            if (debouncer.read() == LOW && debouncer.currentDuration() > BUTTON_HOLD_START_MS) {
                previousState = currentState;
                currentState = State::HOLD_ACTION;
            }
            break;
        case State::SELECT_MENU:
            if (debouncer.read() == LOW && debouncer.currentDuration() > BUTTON_HOLD_START_MS) {
                previousState = currentState;
                currentState = State::HOLD_ACTION;
                break;
            }
            // Act on button release (short press) to cycle menu
            if (debouncer.rose()) {
                menuSelection = (menuSelection + 1) % menuOptionCount;
            }
            break;
        case State::SELECT_RUN_LIMIT:
            if (debouncer.read() == LOW && debouncer.currentDuration() > BUTTON_HOLD_START_MS) {
                previousState = currentState;
                currentState = State::HOLD_ACTION;
                break;
            }
            // Act on button release (short press) to cycle run limit options
            if (debouncer.rose()) {
                runLimitMenuSelection = (runLimitMenuSelection + 1) % runLimitMenuOptionCount;
            }
            break;
        case State::SELECT_DEBUG_MENU:
            if (debouncer.read() == LOW && debouncer.currentDuration() > BUTTON_HOLD_START_MS) {
                previousState = currentState;
                currentState = State::HOLD_ACTION;
                break;
            }
            // Act on button release (short press) to cycle debug options
            if (debouncer.rose()) {
                debugMenuSelection = (debugMenuSelection + 1) % debugMenuOptionCount;
            }
            break;
        case State::HOLD_ACTION:
            // Logic is entirely on release to prevent race conditions and improve UX.
            if (debouncer.rose()) {
                unsigned long heldDuration = debouncer.previousDuration();
                // Define which states allow for a "SELECT" action.
                bool isSelectActionValid = (previousState == State::SETUP ||
                                            previousState == State::SELECT_MENU ||
                                            previousState == State::SELECT_RUN_LIMIT ||
                                            previousState == State::SELECT_DEBUG_MENU);

                // Define which states allow for an "EXIT" action.
                bool isExitClearValid = (previousState == State::AUTO_MODE ||
                                         previousState == State::AUTO_UE4_APERTURE ||
                                         previousState == State::DIRECT_UE4_APERTURE ||
                                         previousState == State::RUNS_COMPLETE);

                // Define which states allow for a "BYPASS" action.
                bool isBypassValid = (previousState == State::DEBUG_MOUSE);

                // Action 1: RESET (highest priority)
                if (heldDuration > BUTTON_RESET_DURATION_MS) {
                    SCB_AIRCR = 0x05FA0004; // Software Reset
                    while(true);
                }
                // Action 2: DEBUG MENU
                else if (heldDuration > BUTTON_DEBUG_DURATION_MS) {
                    // This action is global and always transitions to the debug menu selector.
                    debugMenuSelection = 0; // Reset menu choice
                    currentState = State::SELECT_DEBUG_MENU;
                }
                // Action 3: BYPASS (from mouse debug screen)
                else if (isBypassValid && heldDuration > BUTTON_HOLD_DURATION_MS) {
                    digitalWrite(PIN_LED_BUILTIN, LOW); // Turn off the debug LED
                    menuSelection = 0; // Reset menu choice
                    currentState = State::SELECT_MENU; // Proceed to the main menu
                }
                // Action 4: EXIT (from an active/completed run)
                else if (isExitClearValid && heldDuration > BUTTON_HOLD_DURATION_MS) {
                    // Determine which mode's stats to clear.
                    State modeToClear = (previousState == State::RUNS_COMPLETE) ? selectedMode : previousState;
                    if (modeToClear == State::AUTO_MODE) {
                        statsAuto = LatencyStats();
                    } else if (modeToClear == State::AUTO_UE4_APERTURE) {
                        statsBtoW = LatencyStats(); statsWtoB = LatencyStats(); isFirstUe4Run = true;
                    } else if (modeToClear == State::DIRECT_UE4_APERTURE) {
                        statsDirectBtoW = LatencyStats(); statsDirectWtoB = LatencyStats(); isFirstUe4Run = true;
                    }
                    // Return to the main menu.
                    menuSelection = 0;
                    currentState = State::SELECT_MENU;
                }
                // Action 5: SELECT (only if contextually valid)
                else if (isSelectActionValid && heldDuration > BUTTON_HOLD_DURATION_MS) {
                    // --- CONTEXT-AWARE SELECTION ---
                    if (previousState == State::SELECT_MENU) {
                        // User selected a mode from the main menu.
                        // Store their choice and transition to the run limit menu.
                        if (menuSelection == 0) selectedMode = State::AUTO_MODE;
                        else if (menuSelection == 1) selectedMode = State::AUTO_UE4_APERTURE;
                        else selectedMode = State::DIRECT_UE4_APERTURE;

                        runLimitMenuSelection = 0; // Reset sub-menu choice for a clean start
                        currentState = State::SELECT_RUN_LIMIT;
                    }
                    else if (previousState == State::SELECT_RUN_LIMIT) {
                        // User selected a run limit. Set it and prepare to start the mode.
                        if (runLimitMenuSelection == 0) maxRuns = RUN_LIMIT_OPTION_1;
                        else if (runLimitMenuSelection == 1) maxRuns = RUN_LIMIT_OPTION_2;
                        else if (runLimitMenuSelection == 2) maxRuns = RUN_LIMIT_OPTION_3;
                        else maxRuns = 0; // 0 represents unlimited

                        bool shouldStartMode = true; // Assume we will start unless a check fails.

                        // --- Check for modes requiring a PC connection ---
                        if (selectedMode == State::DIRECT_UE4_APERTURE || selectedMode == State::DEBUG_POLLING_TEST) {
                            if (usb_configuration == 0) {
                                shouldStartMode = false; // Veto the mode start.
                                displayErrorScreen("CONNECTION ERROR", "This mode requires", "a PC connection.", "Returning to menu...");
                                menuSelection = 0; // Reset menu for a clean slate
                                currentState = State::SELECT_MENU; // Go back to the main menu
                            }
                        }

                        // --- Check for modes requiring a mouse connection ---
                        if (shouldStartMode && (selectedMode == State::AUTO_MODE || selectedMode == State::AUTO_UE4_APERTURE) && !mouseIsOk) {
                            shouldStartMode = false; // Veto the mode start.
                            displayErrorScreen("CONNECTION ERROR", "Auto modes require", "a mouse connection.", "Returning to menu...");
                            menuSelection = 0; // Reset menu for a clean slate
                            currentState = State::SELECT_MENU; // Go back to the main menu
                        }

                        if (shouldStartMode) {
                            dataHasBeenSaved = false; // Reset save flag for the new run
                            // Reset stats and data vectors for the selected mode before starting
                            if (selectedMode == State::AUTO_MODE) {
                                statsAuto = LatencyStats();
                                if (ENABLE_SD_LOGGING) latenciesAuto.clear();
                            } else if (selectedMode == State::AUTO_UE4_APERTURE) {
                                ue4_isWaitingForWhite = true; // Reset sub-state
                                isFirstUe4Run = true;
                                statsBtoW = LatencyStats();   // Clear stats
                                statsWtoB = LatencyStats();
                                if (ENABLE_SD_LOGGING) { latenciesBtoW.clear(); latenciesWtoB.clear(); }
                            } else if (selectedMode == State::DIRECT_UE4_APERTURE) {
                                 ue4_isWaitingForWhite = true; // Reset sub-state
                                isFirstUe4Run = true;
                                statsDirectBtoW = LatencyStats(); // Clear stats
                                statsDirectWtoB = LatencyStats();
                                if (ENABLE_SD_LOGGING) { latenciesDirectBtoW.clear(); latenciesDirectWtoB.clear(); }
                            }
                            currentState = selectedMode; // Finally, start the analysis mode
                        }
                    }
                     else if (previousState == State::SELECT_DEBUG_MENU) {
                        // User selected an option from the debug menu.
                        if (debugMenuSelection == 0) {
                            currentState = State::DEBUG_MOUSE;
                        } else if (debugMenuSelection == 1) {
                            currentState = State::DEBUG_LSENSOR;
                        } else { // debugMenuSelection == 2
                             if (usb_configuration == 0) {
                                displayErrorScreen("CONNECTION ERROR", "Polling Test requires", "a PC connection.", "Returning...");
                                currentState = State::SELECT_DEBUG_MENU; // Go back
                            } else {
                                // Draw the polling screen ONCE before entering the high-speed loop.
                                display.clearDisplay();
                                display.setTextSize(1);
                                display.setTextColor(SSD1306_WHITE);
                                drawPollingTestScreen();
                                display.display();

                                // Reset polling test variables before starting.
                                polltest_angle = 0.0f;
                                polltest_last_x = 0;
                                polltest_last_y = 0;
                                currentState = State::DEBUG_POLLING_TEST;
                            }
                        }
                    }
                }
                // Action 6: Aborted hold or invalid action, return to previous state
                else {
                    currentState = previousState;
                }
            }
            break;
        case State::AUTO_MODE: {
            // Check if run limit has been reached BEFORE performing the next measurement.
            if (maxRuns > 0 && statsAuto.runCount >= maxRuns) {
                currentState = State::RUNS_COMPLETE;
                break;
            }

            // Check for periodic save in unlimited mode
            if (maxRuns == 0 && statsAuto.runCount > 0 && (statsAuto.runCount % UNLIMITED_MODE_SAVE_INTERVAL == 0)) {
                saveDataToSD(currentState, 0, true);
            }

            bool timeoutOccurred = false;

            // --- SYNC STEP ---
            // We now wait until the screen has been continuously dark for 4ms.
            elapsedMicros overallSyncTimer; // Overall timeout for the entire sync process.
            elapsedMicros darkStableTimer;  // Timer to measure continuous dark duration.
            bool isCountingDark = false;    // Flag to know if we've started timing a dark period.

            while (true) { // Loop until we confirm 4ms of darkness or timeout.
                if (overallSyncTimer > MEASUREMENT_TIMEOUT_MICROS) {
                    timeoutOccurred = true;
                    break;
                }

                if (fastAnalogRead(PIN_LIGHT_SENSOR) <= DARK_SENSOR_THRESHOLD) {
                    // Screen is dark.
                    if (!isCountingDark) {
                        // This is the first dark reading in a sequence, start the timer.
                        isCountingDark = true;
                        darkStableTimer = 0;
                    }

                    // Check if we have been dark for long enough.
                    if (darkStableTimer > 4000) { // 4000 microseconds = 4 milliseconds.
                        break; // Sync successful.
                    }
                } else {
                    // Screen is not dark, reset the continuous dark timer.
                    isCountingDark = false;
                }

                // A very short delay to prevent busy-looping at max CPU speed.
                delayMicroseconds(50);
            }

            if (timeoutOccurred) {
                // If timeout occurred during sync, skip this run and try again.
                if (delayWithJitterAndAbortCheck(AUTO_MODE_RUN_DELAY_MS)) {
                    previousState = currentState;
                    currentState = State::HOLD_ACTION;
                }
                break; // Skip this run and try again.
            }

            // --- MEASUREMENT STEP ---
            // 1. The latency timer starts THE INSTANT the click signal is sent.
            //    This simulates the mouse button being pressed down, which causes
            //    the RTSS FCAT marker to turn white.
            elapsedMicros latencyTimer;
            digitalWriteFast(PIN_SEND_CLICK, HIGH);

            // 2. Now, wait for the light sensor to detect the screen turning white.
            //    The click signal remains HIGH (mouse button held down) during this period.
            while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                // If the screen doesn't turn white within a reasonable time, it's a measurement timeout.
                if (latencyTimer > MEASUREMENT_TIMEOUT_MICROS) {
                    timeoutOccurred = true;
                    break;
                }
            }

            // 3. After detecting white (or timeout), release the click signal.
            //    This simulates the mouse button release, which should turn the FCAT box black again.
            digitalWriteFast(PIN_SEND_CLICK, LOW);

            // Only update stats if the measurement was successful (no timeout).
            if (!timeoutOccurred) {
                unsigned long latencyMicros = latencyTimer;
                updateStats(statsAuto, latenciesAuto, latencyMicros);
            }

            if (delayWithJitterAndAbortCheck(AUTO_MODE_RUN_DELAY_MS)) {
                previousState = currentState;
                currentState = State::HOLD_ACTION;
            }
            break;
        }
        case State::AUTO_UE4_APERTURE: {
            // Check if run limit has been reached.
            if (maxRuns > 0 && statsBtoW.runCount >= maxRuns) {
                currentState = State::RUNS_COMPLETE;
                break;
            }
            
            // Check for periodic save in unlimited mode
            if (maxRuns == 0 && statsBtoW.runCount > 0 && (statsBtoW.runCount % UNLIMITED_MODE_SAVE_INTERVAL == 0)) {
                saveDataToSD(currentState, 0, true);
            }

            // MODIFIED: On the first run, perform sync AND a warm-up cycle.
            if (isFirstUe4Run) {
                SyncResult syncResult = performSmartSync(false); // false for AUTO mode click

                if (syncResult == SyncResult::HOLD_ABORT) {
                    previousState = currentState;
                    currentState = State::HOLD_ACTION;
                    break;
                }

                if (syncResult == SyncResult::SUCCESS) {
                    // --- WARM-UP CYCLE ---
                    drawSyncScreen("Warming up...", 32);

                    // Warm-up 1: B-to-W (don't measure)
                    digitalWriteFast(PIN_SEND_CLICK, HIGH);
                    delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
                    digitalWriteFast(PIN_SEND_CLICK, LOW);
                    elapsedMicros warmupTimer;
                    while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD && warmupTimer < MEASUREMENT_TIMEOUT_MICROS);
                    delayWithJitterAndAbortCheck(UE4_MODE_RUN_DELAY_MS);

                    // Warm-up 2: W-to-B (don't measure)
                    digitalWriteFast(PIN_SEND_CLICK, HIGH);
                    delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
                    digitalWriteFast(PIN_SEND_CLICK, LOW);
                    warmupTimer = 0;
                    while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD && warmupTimer < MEASUREMENT_TIMEOUT_MICROS);
                    delayWithJitterAndAbortCheck(UE4_MODE_RUN_DELAY_MS);

                    isFirstUe4Run = false;         // Sync and warm-up complete.
                    ue4_isWaitingForWhite = true;  // We ended on DARK, so we expect WHITE next.
                }
                // End this loop cycle. The next one will be the first REAL measurement.
                break;
            }

            // --- This is the normal measurement logic, which now only runs on a "hot" system ---
            bool timeoutOccurred = false;
            elapsedMicros syncTimer;
            if (ue4_isWaitingForWhite) {
                while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD) {
                    if (syncTimer > MEASUREMENT_TIMEOUT_MICROS) { timeoutOccurred = true; break; }
                }
            } else {
                while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                    if (syncTimer > MEASUREMENT_TIMEOUT_MICROS) { timeoutOccurred = true; break; }
                }
            }
            if (timeoutOccurred) {
                if(delayWithJitterAndAbortCheck(UE4_MODE_RUN_DELAY_MS)) { previousState = currentState; currentState = State::HOLD_ACTION; }
                break;
            }

            timeoutOccurred = false;
            if (ue4_isWaitingForWhite) {
                elapsedMicros latencyTimer;
                digitalWriteFast(PIN_SEND_CLICK, HIGH);
                delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
                digitalWriteFast(PIN_SEND_CLICK, LOW);
                while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                    if (latencyTimer > MEASUREMENT_TIMEOUT_MICROS) { timeoutOccurred = true; break; }
                }
                if (!timeoutOccurred) {
                    updateStats(statsBtoW, latenciesBtoW, latencyTimer);
                    ue4_isWaitingForWhite = false;
                }
            } else {
                elapsedMicros latencyTimer;
                digitalWriteFast(PIN_SEND_CLICK, HIGH);
                delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
                digitalWriteFast(PIN_SEND_CLICK, LOW);
                while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD) {
                    if (latencyTimer > MEASUREMENT_TIMEOUT_MICROS) { timeoutOccurred = true; break; }
                }
                if (!timeoutOccurred) {
                    updateStats(statsWtoB, latenciesWtoB, latencyTimer);
                    ue4_isWaitingForWhite = true;
                }
            }
            if (delayWithJitterAndAbortCheck(UE4_MODE_RUN_DELAY_MS)) {
                previousState = currentState;
                currentState = State::HOLD_ACTION;
            }
            break;
        }
        case State::DIRECT_UE4_APERTURE: {
            if (maxRuns > 0 && statsDirectBtoW.runCount >= maxRuns) {
                currentState = State::RUNS_COMPLETE;
                break;
            }

            // Check for periodic save in unlimited mode
            if (maxRuns == 0 && statsDirectBtoW.runCount > 0 && (statsDirectBtoW.runCount % UNLIMITED_MODE_SAVE_INTERVAL == 0)) {
                saveDataToSD(currentState, 0, true);
            }

            // On the first run, perform sync AND a warm-up cycle.
            if (isFirstUe4Run) {
                SyncResult syncResult = performSmartSync(true); // true for DIRECT mode click

                if (syncResult == SyncResult::HOLD_ABORT) {
                    previousState = currentState;
                    currentState = State::HOLD_ACTION;
                    break;
                }

                if (syncResult == SyncResult::SUCCESS) {
                    // --- WARM-UP CYCLE ---
                    drawSyncScreen("Warming up...", 32);

                    // Warm-up 1: B-to-W (don't measure)
                    Mouse.click(MOUSE_LEFT);
                    elapsedMicros warmupTimer;
                    while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD && warmupTimer < MEASUREMENT_TIMEOUT_MICROS);
                    delayWithJitterAndAbortCheck(UE4_MODE_RUN_DELAY_MS);

                    // Warm-up 2: W-to-B (don't measure)
                    Mouse.click(MOUSE_LEFT);
                    warmupTimer = 0;
                    while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD && warmupTimer < MEASUREMENT_TIMEOUT_MICROS);
                    delayWithJitterAndAbortCheck(UE4_MODE_RUN_DELAY_MS);

                    isFirstUe4Run = false;         // Sync and warm-up complete.
                    ue4_isWaitingForWhite = true;  // We ended on DARK, so we expect WHITE next.
                }
                // End this loop cycle. The next one will be the first REAL measurement.
                break;
            }

            // --- Normal measurement logic ---
            bool timeoutOccurred = false;
            elapsedMicros syncTimer;
            if (ue4_isWaitingForWhite) {
                while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD) {
                    if (syncTimer > MEASUREMENT_TIMEOUT_MICROS) { timeoutOccurred = true; break; }
                }
            } else {
                while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                    if (syncTimer > MEASUREMENT_TIMEOUT_MICROS) { timeoutOccurred = true; break; }
                }
            }
            if (timeoutOccurred) {
                if (delayWithJitterAndAbortCheck(UE4_MODE_RUN_DELAY_MS)) { previousState = currentState; currentState = State::HOLD_ACTION; }
                break;
            }

            timeoutOccurred = false;
            if (ue4_isWaitingForWhite) {
                elapsedMicros latencyTimer;
                Mouse.click(MOUSE_LEFT);
                while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                    if (latencyTimer > MEASUREMENT_TIMEOUT_MICROS) { timeoutOccurred = true; break; }
                }
                if (!timeoutOccurred) {
                    updateStats(statsDirectBtoW, latenciesDirectBtoW, latencyTimer);
                    ue4_isWaitingForWhite = false;
                }
            } else {
                elapsedMicros latencyTimer;
                Mouse.click(MOUSE_LEFT);
                while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD) {
                    if (latencyTimer > MEASUREMENT_TIMEOUT_MICROS) { timeoutOccurred = true; break; }
                }
                if (!timeoutOccurred) {
                    updateStats(statsDirectWtoB, latenciesDirectWtoB, latencyTimer);
                    ue4_isWaitingForWhite = true;
                }
            }

            if (delayWithJitterAndAbortCheck(UE4_MODE_RUN_DELAY_MS)) {
                previousState = currentState;
                currentState = State::HOLD_ACTION;
            }
            break;
        }
        case State::DEBUG_POLLING_TEST: {
            // Check for the exit condition: a button click.
            if (debouncer.rose()) {
                currentState = State::SELECT_DEBUG_MENU;
                break;
            }

            // Calculate the new desired cursor position on a circle.
            int current_x = (int)(CIRCLE_RADIUS * cos(polltest_angle));
            int current_y = (int)(CIRCLE_RADIUS * sin(polltest_angle));

            // Calculate the delta (change) from the last position.
            int dx = current_x - polltest_last_x;
            int dy = current_y - polltest_last_y;

            // We call usb_mouse_move() on EVERY loop iteration, without checking if
            // movement occurred. This is the key to achieving a true 8kHz polling rate.
            usb_mouse_move(dx, dy, 0, 0);

            // Store the current position to calculate the next delta.
            polltest_last_x = current_x;
            polltest_last_y = current_y;

            // Increment the angle for the next point on the circle.
            polltest_angle += ANGLE_STEP;

            // Wrap the angle around to prevent it from losing precision.
            if (polltest_angle > TWO_PI) {
                polltest_angle -= TWO_PI;
            }
            break;
        }
        case State::RUNS_COMPLETE:
            // This is a halt state. The display will freeze on the final statistics.
            
            // Save data on completion of a limited run. This runs only once.
            if (!dataHasBeenSaved && maxRuns > 0) {
                saveDataToSD(selectedMode, maxRuns, false);
                dataHasBeenSaved = true;
            }

            // Check for a hold action to allow user to exit.
            if (debouncer.read() == LOW && debouncer.currentDuration() > BUTTON_HOLD_START_MS) {
                previousState = currentState;
                currentState = State::HOLD_ACTION;
            }
            break;
        case State::ERROR_HALT:
            return; // Halt program for critical errors
    }

    // Update the display for all states EXCEPT the polling test, which handles
    // its own display on state entry to allow the loop to run at maximum speed.
    if (currentState != State::DEBUG_POLLING_TEST) {
        updateDisplay();
    }
}

// Replaces delay() with a non-blocking version that checks for an abort signal (button hold).
// Returns true if an abort was detected, false otherwise.
bool delayWithJitterAndAbortCheck(unsigned long baseDelayMs) {
    long jitter = random(-MODE_DELAY_JITTER_MS, MODE_DELAY_JITTER_MS + 1);
    long finalDelay = baseDelayMs + jitter;

    if (finalDelay <= 0) return false;

    elapsedMillis delayTimer;
    while (delayTimer < (unsigned long)finalDelay) {
        debouncer.update();
        if (debouncer.read() == LOW && debouncer.currentDuration() > BUTTON_HOLD_START_MS) {
            return true; // Abort signal detected
        }
        delay(1); // Yield for a moment, allows other processes to run.
    }
    return false; // No abort
}


// Helper function to display a full-screen status message during the sync process.
void drawSyncScreen(const char* message, int y) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    centerText("SYNCHRONIZING", 0);
    display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);
    centerText(message, y);
    display.display();
}

// Performs an intelligent synchronization routine for UE4 modes.
SyncResult performSmartSync(bool isDirectMode) {
    // Announce the sync process
    drawSyncScreen("Sending focus click...");

    // --- Step 1: Send a "focus" click ---
    // This click ensures the target application window has OS focus.
    if (isDirectMode) {
        Mouse.click(MOUSE_LEFT);
    } else {
        digitalWriteFast(PIN_SEND_CLICK, HIGH);
        delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
        digitalWriteFast(PIN_SEND_CLICK, LOW);
    }
    // Give the OS time to react to the focus change.
    if (delayWithJitterAndAbortCheck(250)) return SyncResult::HOLD_ABORT;

    // --- Step 2: Determine the current screen state ---
    drawSyncScreen("Checking state...");
    // Wait for light to stabilize after potential screen changes.
    if (delayWithJitterAndAbortCheck(500)) return SyncResult::HOLD_ABORT;
    int initialState = fastAnalogRead(PIN_LIGHT_SENSOR);

    // --- Step 3: Drive the state to DARK ---
    // We want to end this routine with the screen being black.
    if (initialState >= LIGHT_SENSOR_THRESHOLD) {
        // The screen is WHITE. Send one more click to toggle it to BLACK.
        drawSyncScreen("State is WHITE.", 24);
        centerText("Sending toggle click...", 40);
        display.display();
        if (delayWithJitterAndAbortCheck(500)) return SyncResult::HOLD_ABORT;

        if (isDirectMode) {
            Mouse.click(MOUSE_LEFT);
        } else {
            digitalWriteFast(PIN_SEND_CLICK, HIGH);
            delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
            digitalWriteFast(PIN_SEND_CLICK, LOW);
        }
    } else if (initialState <= DARK_SENSOR_THRESHOLD) {
        // The screen is already DARK. No extra click is needed.
        drawSyncScreen("State is already DARK.");
        if (delayWithJitterAndAbortCheck(1500)) return SyncResult::HOLD_ABORT;
        return SyncResult::SUCCESS; // We are already in the desired state.
    } else {
        // The state is indeterminate (e.g., grey screen, mid-transition).
        drawSyncScreen("Indeterminate state!", 24);
        centerText("Sync failed. Retrying...", 40);
        display.display();
        if (delayWithJitterAndAbortCheck(2000)) return SyncResult::HOLD_ABORT;
        return SyncResult::FAILED;
    }

    // --- Step 4: Verify the screen is now DARK ---
    drawSyncScreen("Verifying DARK state...");
    elapsedMillis verificationTimer;
    while (verificationTimer < 3000) { // 3-second timeout for verification
        if (fastAnalogRead(PIN_LIGHT_SENSOR) <= DARK_SENSOR_THRESHOLD) {
            // Success! The screen is now dark and we are in a known state.
            drawSyncScreen("Sync complete.");
            if (delayWithJitterAndAbortCheck(1000)) return SyncResult::HOLD_ABORT;
            return SyncResult::SUCCESS;
        }
        // Check for abort during verification loop
        if (delayWithJitterAndAbortCheck(5)) return SyncResult::HOLD_ABORT;
    }

    // If we get here, the verification timed out.
    drawSyncScreen("Sync FAILED!", 24);
    centerText("Screen not DARK. Retrying...", 40);
    display.display();
    if (delayWithJitterAndAbortCheck(2000)) return SyncResult::HOLD_ABORT;
    return SyncResult::FAILED;
}

// --- SD Card Functions ---

// Helper to get a string representation of the current mode for filenames
String getModeString(State mode) {
    if (mode == State::AUTO_MODE) return "AUTO";
    if (mode == State::AUTO_UE4_APERTURE) return "AUTO_UE4";
    if (mode == State::DIRECT_UE4_APERTURE) return "DIRECT_UE4";
    return "UNKNOWN";
}

// Finds the next available file number for a given base name to prevent overwriting files.
int getNextFileNumber(const String& path, const String& baseName) {
    int fileNumber = 1;
    while (true) {
        // Using String objects for path manipulation is safer
        String fileName = path + "/" + baseName + "_" + String(fileNumber) + ".csv";
        if (!SD.exists(fileName.c_str())) { // FIX: Use .c_str() for SD library functions
            return fileNumber;
        }
        fileNumber++;
        if (fileNumber > 9999) return -1; // Safety break
    }
}

// Writes the collected latency data to a specified file.
void writeLogFile(const String& filePath, const std::vector<float>& latencies1, const std::vector<float>& latencies2 = {}) {
    File dataFile = SD.open(filePath.c_str(), FILE_WRITE); // FIX: Use .c_str() for SD library functions
    if (dataFile) {
        if (latencies2.empty()) { // Single column log for AUTO mode
            dataFile.println("Latency (ms)");
            for (const auto& latency : latencies1) {
                dataFile.println(latency, 4); // Print with 4 decimal places
            }
        } else { // Dual column log for UE4 modes
            dataFile.println("B-to-W (ms),W-to-B (ms)");
            size_t maxRows = max(latencies1.size(), latencies2.size());
            for (size_t i = 0; i < maxRows; ++i) {
                if (i < latencies1.size()) {
                    dataFile.print(latencies1[i], 4);
                }
                dataFile.print(",");
                if (i < latencies2.size()) {
                    dataFile.print(latencies2[i], 4);
                }
                dataFile.println();
            }
        }
        dataFile.close();
    }
}

// Main function to handle the logic of saving data to the SD card.
void saveDataToSD(State mode, unsigned long run_limit, bool is_partial_save) {
    if (!sdCardPresent || !ENABLE_SD_LOGGING) return;

    String modeStr = getModeString(mode);
    String baseFileName;
    if (run_limit > 0) {
        baseFileName = modeStr + "_" + String(run_limit) + "runs";
    } else {
        baseFileName = modeStr + "_UNLIMITED_part";
    }

    int fileNum = getNextFileNumber(SD_LOG_DIRECTORY, baseFileName);
    if (fileNum == -1) {
        displayErrorScreen("SD CARD ERROR", "Could not find", "a free file name.", "Aborting save...");
        return;
    }
    String filePath = String(SD_LOG_DIRECTORY) + "/" + baseFileName + "_" + String(fileNum) + ".csv";

    // Show "Saving..." message on screen
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    centerText("SAVING LOG...", 16);
    // Display a truncated version of the path if it's too long
    String displayPath = filePath;
    if (displayPath.length() > 21) {
        displayPath = "..." + displayPath.substring(displayPath.length() - 18);
    }
    centerText(displayPath.c_str(), 32);
    display.display();

    // Write the actual file
    if (mode == State::AUTO_MODE) {
        writeLogFile(filePath, latenciesAuto);
        if (is_partial_save) latenciesAuto.clear();
    } else if (mode == State::AUTO_UE4_APERTURE) {
        writeLogFile(filePath, latenciesBtoW, latenciesWtoB);
        if (is_partial_save) { latenciesBtoW.clear(); latenciesWtoB.clear(); }
    } else if (mode == State::DIRECT_UE4_APERTURE) {
        writeLogFile(filePath, latenciesDirectBtoW, latenciesDirectWtoB);
        if (is_partial_save) { latenciesDirectBtoW.clear(); latenciesDirectWtoB.clear(); }
    }

    delay(1000); // Let user see the save message before continuing
}

// --- Optimized Analog Read ---
// Wrapper for the ADC library to perform a faster analog read using our pre-configured settings.
// Marked 'FASTRUN' to be placed in ITCM for maximum speed, as it's called repeatedly inside measurement loops.
FASTRUN int fastAnalogRead(uint8_t pin) {
    return adc->analogRead(pin);
}

// --- Helper function to centralize statistics calculations ---
void updateStats(LatencyStats& stats, std::vector<float>& latencies, unsigned long latencyMicros) {
    float latencyMillis = latencyMicros / 1000.0f;

    // Store raw value for logging if enabled
    if (ENABLE_SD_LOGGING && sdCardPresent) {
        latencies.push_back(latencyMillis);
    }

    // Update the provided stats struct
    stats.runCount++;
    stats.lastLatency = latencyMillis;
    // Use a numerically stable rolling average formula
    stats.avgLatency = stats.avgLatency + (latencyMillis - stats.avgLatency) / stats.runCount;
    if (latencyMillis < stats.minLatency) stats.minLatency = latencyMillis;
    if (latencyMillis > stats.maxLatency) stats.maxLatency = latencyMillis;
}

// --- Helper function to center text on the display ---
void centerText(const char* text, int y) {
    int targetY;

    // Check if a Y coordinate was provided. If not, use the current one.
    if (y == -1) {
        targetY = display.getCursorY();
    } else {
        targetY = y;
    }

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int x = (SCREEN_WIDTH - w) / 2;
    display.setCursor(max(0, x), targetY); // Use max(0, x) to prevent negative coordinates
    display.println(text);
}

// --- Display Functions ---

// Displays a formatted, full-screen error message and then pauses.
void displayErrorScreen(const char* title, const char* line1, const char* line2, const char* line3, unsigned long delayMs) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    centerText(title, 0);
    display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);
    if (line1) centerText(line1, 20);
    if (line2) centerText(line2, 32);
    if (line3) centerText(line3, 48);
    display.display();
    delay(delayMs);
}

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
        case State::SELECT_RUN_LIMIT:
            drawRunLimitMenuScreen();
            break;
        case State::SELECT_DEBUG_MENU:
            drawDebugMenuScreen();
            break;
        case State::HOLD_ACTION:
            drawHoldActionScreen();
            break;
        case State::AUTO_MODE:
        case State::AUTO_UE4_APERTURE:
        case State::DIRECT_UE4_APERTURE:
        case State::RUNS_COMPLETE:
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

// --- Icon Bitmaps ---
// 8x8 checkmark icon for 'OK' status
static const unsigned char PROGMEM check_bmp[] = {
    B00000000, B00000001, B00000011, B00100110, B00110100, B00010000, B00000000, B00000000
};

// 8x8 X icon for 'FAIL' status
static const unsigned char PROGMEM x_bmp[] = {
    B00100010, B00010100, B00001000, B00001000, B00010100, B00100010, B00000000, B00000000
};

// 8x8 dash icon for 'Disabled' status
static const unsigned char PROGMEM dash_bmp[] = {
    B00000000, B00000000, B00000000, B00111100, B00000000, B00000000, B00000000, B00000000
};

// --- Setup Screen Layout ---
void drawSetupScreen(bool monitorOk, bool sensorOk, bool mouseOk, bool sdOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    centerText("SYSTEM CHECK", 0);
    display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);

    const int col1_x = 5;
    const int col2_x = 70;
    const int row1_y = 12;
    const int row2_y = 24;
    const int icon_offset_x = 45;

    // Monitor
    display.setCursor(col1_x, row1_y);
    display.print("Monitor");
    display.drawBitmap(col1_x + icon_offset_x, row1_y, monitorOk ? check_bmp : x_bmp, 8, 8, SSD1306_WHITE);

    // Mouse
    display.setCursor(col2_x, row1_y);
    display.print("Mouse");
    display.drawBitmap(col2_x + icon_offset_x, row1_y, mouseOk ? check_bmp : x_bmp, 8, 8, SSD1306_WHITE);

    // Sensor
    display.setCursor(col1_x, row2_y);
    display.print("Sensor");
    display.drawBitmap(col1_x + icon_offset_x, row2_y, sensorOk ? check_bmp : x_bmp, 8, 8, SSD1306_WHITE);

    // SD Card
    display.setCursor(col2_x, row2_y);
    display.print("SD Card");
    if (ENABLE_SD_LOGGING) {
        display.drawBitmap(col2_x + icon_offset_x, row2_y, sdOk ? check_bmp : x_bmp, 8, 8, SSD1306_WHITE);
    } else {
        display.drawBitmap(col2_x + icon_offset_x, row2_y, dash_bmp, 8, 8, SSD1306_WHITE);
    }
    
    // if (monitorOk && sensorOk && mouseOk) { Removed cus redundant.
    display.drawLine(0, 35, SCREEN_WIDTH - 1, 35, SSD1306_WHITE);
    centerText("Hold Button to Start", 42);

    // Footer
    centerText(GITHUB_TAG, 56);
    display.display();
}

// Hold Action Screen with Progress Bars
void drawHoldActionScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    centerText("Hold for actions", 0);
    display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);

    unsigned long holdTime = debouncer.currentDuration();

    // --- Bar Configuration ---
    const int barWidth = 80;
    const int barX = SCREEN_WIDTH - barWidth - 6;

    // --- SELECT / EXIT / BYPASS Bar ---
    display.setCursor(0, 18);
    bool isSelectValid = (previousState == State::SELECT_MENU || previousState == State::SELECT_RUN_LIMIT || previousState == State::SELECT_DEBUG_MENU || currentState == State::SETUP);
    bool isExitClearValid = (previousState == State::AUTO_MODE || previousState == State::AUTO_UE4_APERTURE || previousState == State::DIRECT_UE4_APERTURE || previousState == State::RUNS_COMPLETE);
    bool isBypassValid = (previousState == State::DEBUG_MOUSE);

    if (isBypassValid) {
        display.print("BYPASS");
    } else if (isSelectValid) {
        display.print("SELECT");
    } else if (isExitClearValid) {
        display.print("EXIT");
    }

    if (isBypassValid || isSelectValid || isExitClearValid) {
        float progress = (float)(holdTime - BUTTON_HOLD_START_MS) / (BUTTON_HOLD_DURATION_MS - BUTTON_HOLD_START_MS);
        progress = constrain(progress, 0.0, 1.0);
        display.drawRect(barX, 16, barWidth, 10, SSD1306_WHITE);
        display.fillRect(barX, 16, (int)(barWidth * progress), 10, SSD1306_WHITE);
    }

    // --- DEBUG Bar ---
    display.setCursor(0, 34);
    if (previousState != State::SELECT_DEBUG_MENU) {
        display.print("DEBUG");
        float debugProgress = (float)(holdTime - BUTTON_HOLD_START_MS) / (BUTTON_DEBUG_DURATION_MS - BUTTON_HOLD_START_MS);
        debugProgress = constrain(debugProgress, 0.0, 1.0);
        display.drawRect(barX, 32, barWidth, 10, SSD1306_WHITE);
        display.fillRect(barX, 32, (int)(barWidth * debugProgress), 10, SSD1306_WHITE);
    }

    // --- RESET Bar ---
    display.setCursor(0, 50);
    display.print("RESET");
    float resetProgress = (float)(holdTime - BUTTON_HOLD_START_MS) / (BUTTON_RESET_DURATION_MS - BUTTON_HOLD_START_MS);
    resetProgress = constrain(resetProgress, 0.0, 1.0);
    display.drawRect(barX, 48, barWidth, 10, SSD1306_WHITE);
    display.fillRect(barX, 48, (int)(barWidth * resetProgress), 10, SSD1306_WHITE);
}

void drawMouseDebugScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    centerText("MOUSE DEBUG", 0);
    display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);

    // Live Data
    int rawValue = fastAnalogRead(PIN_MOUSE_PRESENCE);
    float voltage = (rawValue / 255.0f) * 3.3f; // Convert 8-bit ADC to voltage
    char voltageStr[8];
    dtostrf(voltage, 4, 2, voltageStr); // Format voltage to "X.XX"

    display.setCursor(0, 16);
    display.print("Raw: ");
    display.print(rawValue);

    display.setCursor(68, 16); // Move to the right
    display.print(voltageStr);
    display.print("V");

    display.setCursor(0, 28);
    display.print("Min ADC Lvl: >");
    display.print(MOUSE_PRESENCE_MIN_ADC_VALUE);

    display.setCursor(0, 40);
    display.print("Max Fluct: <");
    display.print(MOUSE_STABILITY_THRESHOLD_ADC);

    // Footer
    centerText(GITHUB_TAG, 56);
}

void drawLightSensorDebugScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    centerText("LSENSOR DEBUG", 0);
    display.drawLine(0, 8, SCREEN_WIDTH-1, 8, SSD1306_WHITE);

    // Live Data
    int rawValue = fastAnalogRead(PIN_LIGHT_SENSOR);

    display.setCursor(0, 16);
    display.print("Pin: ");
    display.print(PIN_LIGHT_SENSOR);

    display.setCursor(0, 28);
    display.print("Live Reading: ");
    display.print(rawValue);
    display.setCursor(0, 40);
    display.print("Fails if Fluct >");
    display.print(SENSOR_FLUCTUATION_THRESHOLD);

    // Footer
    centerText(GITHUB_TAG, 56);
}

void drawPollingTestScreen() {
    centerText("POLLING TEST", 0);
    display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);

    centerText("Mouse moving...", 20);
    centerText("Use HamsterWheel", 32);
    centerText("Click button to exit.", 44);

    centerText(GITHUB_TAG, 56);
}

// --- Generic menu drawing function to reduce code duplication ---
void drawGenericMenu(const char* title, const char* const options[], int optionCount, int selection, bool includeFooter = true) {
    centerText(title, 0);
    display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);

    for (int i = 0; i < optionCount; ++i) {
        display.setCursor(10, 16 + i * 12);
        if (i == selection) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        display.println(options[i]);
    }

    // Footer
    if (includeFooter) centerText(GITHUB_TAG, 56);
}

void drawMenuScreen() {
    const char* const menuOptions[] = {"Automatic", "Auto UE4", "Direct UE4"};
    drawGenericMenu("Select Mode", menuOptions, menuOptionCount, menuSelection);
}

void drawRunLimitMenuScreen() {
    char opt1[16], opt2[16], opt3[16];
    sprintf(opt1, "%lu Runs", RUN_LIMIT_OPTION_1);
    sprintf(opt2, "%lu Runs", RUN_LIMIT_OPTION_2);
    sprintf(opt3, "%lu Runs", RUN_LIMIT_OPTION_3);
    const char* const runLimitOptions[] = {opt1, opt2, opt3, "Unlimited"};
    drawGenericMenu("Select Run Limit", runLimitOptions, runLimitMenuOptionCount, runLimitMenuSelection, false);
}

void drawDebugMenuScreen() {
    const char* const debugOptions[] = {"Mouse Debug", "LSensor Debug", "Polling Test"};
    drawGenericMenu("Debug Menu", debugOptions, debugMenuOptionCount, debugMenuSelection);
}

// --- Main Operation Screen Router ---
void drawOperationScreen() {
    // Determine which mode's stats to display.
    // In RUNS_COMPLETE state, 'selectedMode' holds the mode that was just finished.
    State modeToDisplay = (currentState == State::RUNS_COMPLETE) ? selectedMode : currentState;

    if (modeToDisplay == State::AUTO_MODE) {
        drawAutoModeStats();
    } else if (modeToDisplay == State::AUTO_UE4_APERTURE) {
        drawUe4StatsScreen("Auto UE4 Aperture", statsBtoW, statsWtoB);
    } else if (modeToDisplay == State::DIRECT_UE4_APERTURE) {
        drawUe4StatsScreen("Direct UE4 Aperture", statsDirectBtoW, statsDirectWtoB);
    }
}

// --- Specific Stat Screens ---
void drawAutoModeStats() {
    char buf[16];

    // Mode Title (Top-Right)
    display.setCursor(88, 0); // Adjusted for "AUTO" width
    display.print("AUTO");

    // Horizontal divider line
    display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);

    // Last Latency
    dtostrf(statsAuto.lastLatency, 7, 4, buf);
    display.setCursor(0, 15);
    display.print("Last: ");
    display.print(buf);
    display.print("ms");

    // Average Latency
    dtostrf(statsAuto.avgLatency, 7, 4, buf);
    display.setCursor(0, 28);
    display.print("Avg:  ");
    display.print(buf);
    display.print("ms");

    // Min/Max
    display.setTextSize(1);
    dtostrf(statsAuto.minLatency, 6, 3, buf);
    display.setCursor(0, 41);
    display.print("Min:");
    display.print(buf);

    dtostrf(statsAuto.maxLatency, 6, 3, buf);
    display.setCursor(64, 41);
    display.print("Max:");
    display.print(buf);

    // --- Footer ---
    // Watermark (Bottom-Left)
    display.setCursor(0, 56);
    display.print("S4N-T0S");

    // Run count (Bottom-Right)
    char runBuf[20];
    if (currentState == State::RUNS_COMPLETE) {
        sprintf(runBuf, "DONE | %lu", statsAuto.runCount);
    } else {
        sprintf(runBuf, "Runs: %lu", statsAuto.runCount);
    }
    int runWidth = strlen(runBuf) * 6;
    display.setCursor(max(0, SCREEN_WIDTH - runWidth - 6), 56);
    display.print(runBuf);
}

// Refactored function to display stats for any UE4-style mode to reduce code duplication
void drawUe4StatsScreen(const char* title, const LatencyStats& b_to_w_stats, const LatencyStats& w_to_b_stats) {
    char buf[16];

    // --- Title ---
    centerText(title, 0);

    // --- Column Headers ---
    display.setCursor(0, 12);
    display.print("B-to-W");
    display.setCursor(74, 12);
    display.print("W-to-B");
    display.drawLine(64, 10, 64, 54, SSD1306_WHITE); // Vertical divider

    // --- Last Latency ---
    dtostrf(b_to_w_stats.lastLatency, 7, 4, buf);
    display.setCursor(0, 21);
    display.print("L:"); display.print(buf);

    dtostrf(w_to_b_stats.lastLatency, 7, 4, buf);
    display.setCursor(68, 21);
    display.print("L:"); display.print(buf);

    // --- Average Latency ---
    dtostrf(b_to_w_stats.avgLatency, 7, 4, buf);
    display.setCursor(0, 30);
    display.print("A:"); display.print(buf);

    dtostrf(w_to_b_stats.avgLatency, 7, 4, buf);
    display.setCursor(68, 30);
    display.print("A:"); display.print(buf);

    // --- Min/Max Latency ---
    dtostrf(b_to_w_stats.minLatency, 6, 3, buf);
    display.setCursor(0, 39);
    display.print("m:"); display.print(buf);

    dtostrf(w_to_b_stats.minLatency, 6, 3, buf);
    display.setCursor(68, 39);
    display.print("m:"); display.print(buf);

    dtostrf(b_to_w_stats.maxLatency, 6, 3, buf);
    display.setCursor(0, 48);
    display.print("M:"); display.print(buf);

    dtostrf(w_to_b_stats.maxLatency, 6, 3, buf);
    display.setCursor(68, 48);
    display.print("M:"); display.print(buf);

    // --- Footer ---
    // Watermark (Bottom-Left)
    display.setCursor(0, 56);
    display.print("S4N-T0S");

    // Run count (Bottom-Right)
    char runBuf[20];
    if (currentState == State::RUNS_COMPLETE) {
         sprintf(runBuf, "DONE | %lu", b_to_w_stats.runCount);
    } else {
        // or w_to_b_stats.runCount, they should be the same
         sprintf(runBuf, "Runs: %lu", b_to_w_stats.runCount);
    }
    int runWidth = strlen(runBuf) * 6;
    display.setCursor(max(0, SCREEN_WIDTH - runWidth - 6), 56);
    display.print(runBuf);
}


// --- Error Handling ---
void enterErrorState(const char* errorMessage) {
    currentState = State::ERROR_HALT;
    digitalWrite(PIN_LED_BUILTIN, HIGH); // Turn on solid LED to indicate a critical error
    // The display will be left as it was, or will be blank if it failed to init.
}