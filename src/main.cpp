#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Bounce2.h>
#include <elapsedMillis.h>
#include <Entropy.h>
#include <ADC.h> // Teensy-specific ADC library for high-speed analog reads
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
    DEBUG_LSENSOR
};
State currentState = State::SETUP;
State previousState = State::SETUP;
State selectedMode = State::SETUP;

// --- Menu Variables ---
int menuSelection = 0;
const int menuOptionCount = 3;
int runLimitMenuSelection = 0;
const int runLimitMenuOptionCount = 4;
int debugMenuSelection = 0;
const int debugMenuOptionCount = 2;
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

// --- UE4 Mode State ---
// This tracks whether the next measurement should be Black-to-White or White-to-Black
bool ue4_isWaitingForWhite = true;
bool isFirstUe4Run = true;

// --- Forward Declarations ---
void updateDisplay();
void drawSetupScreen(bool monitorOk, bool sensorOk, bool mouseOk);
void drawMenuScreen();
void drawRunLimitMenuScreen();
void drawDebugMenuScreen();
void drawHoldActionScreen();
void drawOperationScreen();
void drawAutoModeStats();
void drawUe4StatsScreen(const char* title, const LatencyStats& b_to_w_stats, const LatencyStats& w_to_b_stats);
void drawMouseDebugScreen();
void drawLightSensorDebugScreen();
void enterErrorState(const char* errorMessage);
void updateStats(LatencyStats& stats, unsigned long latencyMicros);
int fastAnalogRead(uint8_t pin);
void drawSyncScreen(const char* message, int y = 32);
bool performSmartSync(bool isDirectMode);
void centerText(const char* text, int y);
void delayWithJitter(unsigned long baseDelayMs);

// --- Setup Function ---
void setup() {
    pinMode(PIN_LED_BUILTIN, OUTPUT);
    digitalWrite(PIN_LED_BUILTIN, LOW);

    // Seed the software pseudo-random generator with a true random number.
    randomSeed(Entropy.random());

    // Initialize USB Mouse functionality. (Only used for direct)
    Mouse.begin();

    // Set the click pin to output mode. The pinMode function is sufficient to
    // configure it for GPIO use, which allows for direct register manipulation later.
    pinMode(PIN_SEND_CLICK, OUTPUT);
    // Set the initial state to LOW using a fast method to ensure it's off before the loop begins.
    digitalWriteFast(PIN_SEND_CLICK, LOW);

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

    // Check for mouse presence by measuring the stability (fluctuation/voltage) on the analog pin.
    int minMouseReading = 1023;
    int maxMouseReading = 0;
    componentCheckTimer = 0; // Reset the timer for the next check
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
    bool mouseOk = isStable && isHighEnough;

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
            drawSetupScreen(monitorOk, sensorOk, mouseOk);
        }
    }
}

// --- Main Loop ---
void loop() {
    // Always update the debouncer for any state that might use the button
    debouncer.update();

    // Global check to enter the HOLD_ACTION state from any operational state
    bool canEnterHold = (currentState == State::SELECT_MENU || currentState == State::SELECT_RUN_LIMIT ||
                         currentState == State::SELECT_DEBUG_MENU || currentState == State::AUTO_MODE ||
                         currentState == State::AUTO_UE4_APERTURE || currentState == State::DIRECT_UE4_APERTURE ||
                         currentState == State::DEBUG_MOUSE || currentState == State::DEBUG_LSENSOR ||
                         currentState == State::RUNS_COMPLETE);
    if (canEnterHold && debouncer.read() == LOW && debouncer.currentDuration() > BUTTON_HOLD_START_MS) {
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
        case State::SELECT_RUN_LIMIT:
            // Act on button release (short press) to cycle run limit options
            if (debouncer.rose()) {
                runLimitMenuSelection = (runLimitMenuSelection + 1) % runLimitMenuOptionCount;
            }
            break;
        case State::SELECT_DEBUG_MENU:
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
                // Action 3: SELECT (only if contextually valid)
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
                        if (runLimitMenuSelection == 0) maxRuns = 100;
                        else if (runLimitMenuSelection == 1) maxRuns = 300;
                        else if (runLimitMenuSelection == 2) maxRuns = 500;
                        else maxRuns = 0; // 0 represents unlimited

                        bool shouldStartMode = true; // Assume we will start unless the Direct check fails.

                        // --- Check for Direct modes ---
                        if (selectedMode == State::DIRECT_UE4_APERTURE) {
                            if (usb_configuration == 0) {
                                shouldStartMode = false; // Veto the mode start.

                                // Display error message to the user.
                                display.clearDisplay();
                                display.setTextSize(1);
                                display.setTextColor(SSD1306_WHITE);
                                centerText("CONNECTION ERROR", 0);
                                display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, SSD1306_WHITE);
                                centerText("Direct Mode requires", 20);
                                centerText("a PC connection.", 32);
                                centerText("Returning to menu...", 48);
                                display.display();
                                delay(3500);

                                menuSelection = 0; // Reset menu for a clean slate
                                currentState = State::SELECT_MENU; // Go back to the main menu
                            }
                        }

                        if (shouldStartMode) {
                            // Reset stats for the selected mode before starting a new test session
                            if (selectedMode == State::AUTO_MODE) {
                                statsAuto = LatencyStats();
                            } else if (selectedMode == State::AUTO_UE4_APERTURE) {
                                ue4_isWaitingForWhite = true; // Reset sub-state
                                isFirstUe4Run = true;
                                statsBtoW = LatencyStats();   // Clear stats
                                statsWtoB = LatencyStats();
                            } else if (selectedMode == State::DIRECT_UE4_APERTURE) {
                                 ue4_isWaitingForWhite = true; // Reset sub-state
                                isFirstUe4Run = true;
                                statsDirectBtoW = LatencyStats(); // Clear stats
                                statsDirectWtoB = LatencyStats();
                            }
                            currentState = selectedMode; // Finally, start the analysis mode
                        }
                    }
                     else if (previousState == State::SELECT_DEBUG_MENU) {
                        // User selected an option from the debug menu.
                        if (debugMenuSelection == 0) {
                            currentState = State::DEBUG_MOUSE;
                        } else {
                            currentState = State::DEBUG_LSENSOR;
                        }
                    }
                }
                // Action 4: Aborted hold or invalid action, return to previous state
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

            bool timeoutOccurred = false;

            // --- SYNC STEP ---
            // We now wait until the screen has been continuously dark for 4ms.
            elapsedMicros overallSyncTimer; // Overall timeout for the entire sync process.
            elapsedMicros darkStableTimer;  // Timer to measure continuous dark duration.
            bool isCountingDark = false;    // Flag to know if we've started timing a dark period.

            while (true) { // Loop until we confirm 4ms of darkness or timeout.
                if (overallSyncTimer > 2000000) {
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
                delayWithJitter(AUTO_MODE_RUN_DELAY_MS);
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
                if (latencyTimer > 2000000) { // 2 second timeout for measurement
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
                updateStats(statsAuto, latencyMicros);
            }

            delayWithJitter(AUTO_MODE_RUN_DELAY_MS);
            break;
        }
        case State::AUTO_UE4_APERTURE: {
            // Check if run limit has been reached.
            if (maxRuns > 0 && statsBtoW.runCount >= maxRuns) {
                currentState = State::RUNS_COMPLETE;
                break;
            }

            // MODIFIED: On the first run, perform sync AND a warm-up cycle.
            if (isFirstUe4Run) {
                bool syncSuccess = performSmartSync(false); // false for AUTO mode click

                if (syncSuccess) {
                    // --- WARM-UP CYCLE ---
                    drawSyncScreen("Warming up...", 32);

                    // Warm-up 1: B-to-W (don't measure)
                    digitalWriteFast(PIN_SEND_CLICK, HIGH);
                    delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
                    digitalWriteFast(PIN_SEND_CLICK, LOW);
                    elapsedMicros warmupTimer;
                    while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD && warmupTimer < 2000000);
                    delayWithJitter(UE4_MODE_RUN_DELAY_MS);

                    // Warm-up 2: W-to-B (don't measure)
                    digitalWriteFast(PIN_SEND_CLICK, HIGH);
                    delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
                    digitalWriteFast(PIN_SEND_CLICK, LOW);
                    warmupTimer = 0;
                    while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD && warmupTimer < 2000000);
                    delayWithJitter(UE4_MODE_RUN_DELAY_MS);

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
                    if (syncTimer > 2000000) { timeoutOccurred = true; break; }
                }
            } else {
                while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                    if (syncTimer > 2000000) { timeoutOccurred = true; break; }
                }
            }
            if (timeoutOccurred) { delayWithJitter(UE4_MODE_RUN_DELAY_MS); break; }

            timeoutOccurred = false;
            if (ue4_isWaitingForWhite) {
                elapsedMicros latencyTimer;
                digitalWriteFast(PIN_SEND_CLICK, HIGH);
                delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
                digitalWriteFast(PIN_SEND_CLICK, LOW);
                while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                    if (latencyTimer > 2000000) { timeoutOccurred = true; break; }
                }
                if (!timeoutOccurred) {
                    updateStats(statsBtoW, latencyTimer);
                    ue4_isWaitingForWhite = false;
                }
            } else {
                elapsedMicros latencyTimer;
                digitalWriteFast(PIN_SEND_CLICK, HIGH);
                delayMicroseconds(MOUSE_CLICK_HOLD_MICROS);
                digitalWriteFast(PIN_SEND_CLICK, LOW);
                while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD) {
                    if (latencyTimer > 2000000) { timeoutOccurred = true; break; }
                }
                if (!timeoutOccurred) {
                    updateStats(statsWtoB, latencyTimer);
                    ue4_isWaitingForWhite = true;
                }
            }
            delayWithJitter(UE4_MODE_RUN_DELAY_MS);
            break;
        }
        case State::DIRECT_UE4_APERTURE: {
            if (maxRuns > 0 && statsDirectBtoW.runCount >= maxRuns) {
                currentState = State::RUNS_COMPLETE;
                break;
            }

            // On the first run, perform sync AND a warm-up cycle.
            if (isFirstUe4Run) {
                bool syncSuccess = performSmartSync(true); // true for DIRECT mode click

                if (syncSuccess) {
                    // --- WARM-UP CYCLE ---
                    drawSyncScreen("Warming up...", 32);

                    // Warm-up 1: B-to-W (don't measure)
                    Mouse.click(MOUSE_LEFT);
                    elapsedMicros warmupTimer;
                    while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD && warmupTimer < 2000000);
                    delayWithJitter(UE4_MODE_RUN_DELAY_MS);

                    // Warm-up 2: W-to-B (don't measure)
                    Mouse.click(MOUSE_LEFT);
                    warmupTimer = 0;
                    while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD && warmupTimer < 2000000);
                    delayWithJitter(UE4_MODE_RUN_DELAY_MS);

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
                    if (syncTimer > 2000000) { timeoutOccurred = true; break; }
                }
            } else {
                while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                    if (syncTimer > 2000000) { timeoutOccurred = true; break; }
                }
            }
            if (timeoutOccurred) { delayWithJitter(UE4_MODE_RUN_DELAY_MS); break; }

            timeoutOccurred = false;
            if (ue4_isWaitingForWhite) {
                elapsedMicros latencyTimer;
                Mouse.click(MOUSE_LEFT);
                while (fastAnalogRead(PIN_LIGHT_SENSOR) < LIGHT_SENSOR_THRESHOLD) {
                    if (latencyTimer > 2000000) { timeoutOccurred = true; break; }
                }
                if (!timeoutOccurred) {
                    updateStats(statsDirectBtoW, latencyTimer);
                    ue4_isWaitingForWhite = false;
                }
            } else {
                elapsedMicros latencyTimer;
                Mouse.click(MOUSE_LEFT);
                while (fastAnalogRead(PIN_LIGHT_SENSOR) > DARK_SENSOR_THRESHOLD) {
                    if (latencyTimer > 2000000) { timeoutOccurred = true; break; }
                }
                if (!timeoutOccurred) {
                    updateStats(statsDirectWtoB, latencyTimer);
                    ue4_isWaitingForWhite = true;
                }
            }
            delayWithJitter(UE4_MODE_RUN_DELAY_MS);
            break;
        }
        case State::RUNS_COMPLETE:
            // This is a halt state. No actions are performed.
            // The display will freeze on the final statistics from the last run.
            // Hold the button to reset the device.
            break;
        case State::ERROR_HALT:
            return; // Halt program for critical errors
    }
    // Update the display in every loop cycle for all active states
    updateDisplay();
}

// Helper function to add random jitter to delays between measurement runs.
void delayWithJitter(unsigned long baseDelayMs) {
    // Adding random jitter breaks any potential phase-lock between the Teensy's
    // loop and the game's render loop, ensuring more statistically accurate
    // sampling over many runs.
    // The `random()` function's max value is exclusive, so im adding 1 to make it inclusive.
    long jitter = random(-MODE_DELAY_JITTER_MS, MODE_DELAY_JITTER_MS + 1);

    // Ensure the final delay is not negative if jitter is large and negative.
    long finalDelay = baseDelayMs + jitter;
    if (finalDelay > 0) {
        delay(finalDelay);
    }
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
bool performSmartSync(bool isDirectMode) {
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
    delay(250); // Give the OS time to react to the focus change.

    // --- Step 2: Determine the current screen state ---
    drawSyncScreen("Checking state...");
    delay(500); // Wait for light to stabilize after potential screen changes.
    int initialState = fastAnalogRead(PIN_LIGHT_SENSOR);

    // --- Step 3: Drive the state to DARK ---
    // We want to end this routine with the screen being black.
    if (initialState >= LIGHT_SENSOR_THRESHOLD) {
        // The screen is WHITE. Send one more click to toggle it to BLACK.
        drawSyncScreen("State is WHITE.", 24);
        centerText("Sending toggle click...", 40);
        display.display();
        delay(500);

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
        delay(1500); // Hold message for user
        return true; // We are already in the desired state.
    } else {
        // The state is indeterminate (e.g., grey screen, mid-transition).
        drawSyncScreen("Indeterminate state!", 24);
        centerText("Sync failed. Retrying...", 40);
        display.display();
        delay(2000);
        return false;
    }

    // --- Step 4: Verify the screen is now DARK ---
    drawSyncScreen("Verifying DARK state...");
    elapsedMillis verificationTimer;
    while (verificationTimer < 3000) { // 3-second timeout for verification
        if (fastAnalogRead(PIN_LIGHT_SENSOR) <= DARK_SENSOR_THRESHOLD) {
            // Success! The screen is now dark and we are in a known state.
            drawSyncScreen("Sync complete.");
            delay(1000); // Hold message for user
            return true;
        }
    }

    // If we get here, the verification timed out.
    drawSyncScreen("Sync FAILED!", 24);
    centerText("Screen not DARK. Retrying...", 40);
    display.display();
    delay(2000);
    return false;
}

// --- Optimized Analog Read ---
// Wrapper for the ADC library to perform a faster analog read using our pre-configured settings.
// Marked 'FASTRUN' to be placed in ITCM for maximum speed, as it's called repeatedly inside measurement loops.
FASTRUN int fastAnalogRead(uint8_t pin) {
    return adc->analogRead(pin);
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

// --- Helper function to center text on the display ---
void centerText(const char* text, int y = -1) {
    int targetY;

    // Check if a Y coordinate was provided. If not, use the current one.
    if (y == -1) {
        targetY = display.getCursorY();
    } else {
        targetY = y;
    }

    int textWidth = strlen(text) * 6; // Using default font width of 6 pixels
    int x = (SCREEN_WIDTH - textWidth) / 2;
    display.setCursor(max(0, x), targetY); // Use max(0, x) to prevent negative coordinates
    display.println(text);
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

void drawSetupScreen(bool monitorOk, bool sensorOk, bool mouseOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    centerText("SETUP MODE", 0);
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

    // --- SELECT Bar ---
    display.setCursor(0, 18);
    // Only show "SELECT" as an option if it's a valid action from the current state
    if (previousState == State::SELECT_MENU || previousState == State::SELECT_RUN_LIMIT || previousState == State::SELECT_DEBUG_MENU || currentState == State::SETUP) {
        display.print("SELECT");
        float selectProgress = (float)(holdTime - BUTTON_HOLD_START_MS) / (BUTTON_HOLD_DURATION_MS - BUTTON_HOLD_START_MS);
        selectProgress = constrain(selectProgress, 0.0, 1.0);
        display.drawRect(barX, 16, barWidth, 10, SSD1306_WHITE);
        display.fillRect(barX, 16, (int)(barWidth * selectProgress), 10, SSD1306_WHITE);
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
    const char* const runLimitOptions[] = {"100 Runs", "300 Runs", "500 Runs", "Unlimited"};
    drawGenericMenu("Select Run Limit", runLimitOptions, runLimitMenuOptionCount, runLimitMenuSelection, false);
}

void drawDebugMenuScreen() {
    const char* const debugOptions[] = {"Mouse Debug", "LSensor Debug"};
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