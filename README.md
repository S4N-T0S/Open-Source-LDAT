# Open-Source-LDAT Teensy 4.1 (Latency Analyser)

![Latency Analyzer in Action](https://github.com/S4N-T0S/Open-Source-LDAT/blob/main/readme_media/Open-Source-LDAT_S4N-T0S_Showcase.gif)

This project provides the firmware for a powerful, DIY end-to-end system latency tester, similar in concept to NVIDIA's LDAT. Built around the high-performance Teensy 4.1 microcontroller, this tool measures the true "click-to-photon" latency of your system—the time from a mouse click being initiated to the corresponding change appearing on your display.

It is designed to be highly accurate, leveraging Teensy-specific optimizations for high-speed I/O and direct hardware manipulation. The device features a clear OLED display for real-time statistics and a simple, intuitive one-button interface.

## Key Features

*   **High-Speed Measurement:** Utilizes the Teensy's `ADC` library for fast analog reads and `digitalWriteFast` for minimal I/O overhead.
*   **Multiple Testing Modes:** Includes a general-purpose automatic mode and specialized modes for testing with controlled software like the Aperture Grille test utility.
*   **True 8kHz Polling:** A "Direct Inject" build script temporarily patches the Teensy core to enable a true 8000 Hz USB polling rate for the most accurate "Direct" mode testing.
*   **On-Device Real-Time Stats:** The OLED screen displays live data including last, average, minimum, and maximum latency, plus run count.
*   **Robust Hardware Diagnostics:** A comprehensive setup mode runs on boot to verify all components are connected and functioning correctly.
*   **Simple One-Button UI:** A clever control scheme allows full navigation, selection, and system resets with just a single push button.

---

## Hardware Requirements

This project requires soldering and basic electronics knowledge. The core components are:

*   **Microcontroller:** Teensy 4.1
*   **Light Sensor:** TEMT6000 Ambient Light Sensor
*   **Display:** 128x64 I2C SSD1306 OLED Display (White or Blue/Yellow)
*   **Input:** A standard momentary push button
*   **Transistor:** BC547A NPN Transistor (or similar)
*   **Resistor:** 220 Ohm Resistor
*   **Host Mouse:** An old or spare mouse that can be sacrificed for its switch and cable (optical sensors slightly more difficult to solder).

![Wiring Diagram](https://github.com/S4N-T0S/Open-Source-LDAT/blob/main/readme_media/Open-Source-LDAT_S4N-T0S_Wiring.jpg)

---

## Software & Environment

This firmware is designed to be compiled and uploaded using **Visual Studio Code** with the **PlatformIO** extension.

### Why PlatformIO?

PlatformIO manages toolchains, libraries, and board-specific configurations automatically. Most importantly, it allows us to run a custom Python script (`8kHz_polling.py`) during the build process. This script is crucial as it **temporarily patches the Teensyduino core files** to change the USB mouse polling interval from the default 250µs (4000 Hz) to 125µs (8000 Hz), enabling the most accurate direct latency measurements. The script automatically creates a backup and restores the original file after compilation.

### A Note on Display Libraries

We use the Adafruit GFX and SSD1306 libraries for the OLED display. While libraries like `U8g2` can be faster for rendering, the display's performance has **no impact on the latency measurement itself**. The measurement timing is handled by dedicated high-speed functions. Therefore, the Adafruit libraries were chosen for their simplicity and robustness without compromising the accuracy of the results.

---

## Getting Started: Installation & Configuration

### Step 1: **IMPORTANT** - Configure Your `config.h`

Before compiling, you **must** customize the `include/config.h` file. This is the central configuration file for the entire project.

1.  **Pinout:** Verify that the pin numbers (`PIN_BUTTON`, `PIN_SEND_CLICK`, `PIN_MOUSE_PRESENCE`, etc.) match your physical wiring.
2.  **Thresholds:** The default sensor and mouse detection thresholds will likely need tuning for your specific components and ambient lighting conditions.
    *   `LIGHT_SENSOR_THRESHOLD` / `DARK_SENSOR_THRESHOLD`: Place the sensor on your monitor. Use the `DEBUG_LSENSOR` mode to see live readings for a white screen and a black screen, then set your thresholds accordingly.
    *   `MOUSE_PRESENCE_MIN_ADC_VALUE` / `MOUSE_STABILITY_THRESHOLD_ADC`: These values are used to confirm a mouse is connected. Use the `DEBUG_MOUSE` mode to see the live reading and adjust if needed.
3.  **Click Timing:**
    *   `MOUSE_CLICK_HOLD_MICROS`: This value dictates how long the click signal is held. It should be tuned based on your system's polling rate to ensure the click is reliably detected. The comments in the file provide safe starting points for various polling rates (1kHz, 4kHz, 8kHz).

### Step 2: Compile and Upload

1.  Open the project folder in Visual Studio Code (with PlatformIO installed).
2.  Connect your Teensy 4.1 to your computer.
3.  Click the **Upload** button in the PlatformIO toolbar (the right-pointing arrow).
4.  PlatformIO will automatically download the required libraries, run the 8kHz patch script, compile the firmware, and upload it to the Teensy.

---

## How to Use the Device

The device is controlled with a single button, which has three actions.

### The One-Button UI

*   **Short Press (Click):** A quick press and release. This is used to cycle through options in a menu.
*   **Long Press (Select):** Press and hold the button for about 0.75 seconds. A progress bar will fill on the screen. Releasing after it's full **selects** the highlighted menu option.
*   **Reset Press (Reset):** Press and hold the button for about 1.5 seconds. A second progress bar for "RESET" will fill. Releasing the button will trigger a software reset of the device. This can be done from almost any screen.

### On-Boot: The Setup Screen

When the device first boots, it enters `SETUP MODE` and performs a hardware diagnostic.
*   **Monitor:** Checks if the OLED display is connected.
*   **Sensor:** Checks if the light sensor is stable and not fluctuating wildly.
*   **Mouse:** Checks for the presence of a connected mouse.

If all checks pass, it will prompt you to "Hold Button Now" to proceed. If a check fails, the program will halt on a debug screen for that component, and the built-in LED on the Teensy will blink to indicate an error.

---

## Operating Modes Explained

### 1. Automatic Mode

This is a general-purpose latency test.
*   **What it does:** The device sends a click signal directly through its output pin and immediately starts a timer. It then waits for the light sensor to detect the screen changing from dark to light, stops the timer, and records the result.
*   **Use case:** Excellent for testing the latency of a monitor or getting a baseline system reading without involving the USB stack.

### 2. Auto UE4 Aperture Mode

This mode is specifically designed to work with the **Aperture Grille Latency Tester** software. This is the **highly recommended** mode for initial setup and calibration.

*   **Website:** [Aperture Grille Software](https://www.aperturegrille.com/software/)
*   **What it does:** It performs the same function as Automatic Mode but is tailored for the B/W and W/B toggle in the Aperture Grille app. It measures both Black-to-White and White-to-Black transitions separately. Before starting, it runs a smart "sync" and "warm-up" routine to ensure the test is synchronized with the application and the system is ready.
*   **Use case:** Provides a controlled, repeatable environment for testing, making it perfect for comparing settings like VSync, frame caps, or driver options.

### 3. Direct UE4 Aperture Mode

The simplest measuring mode, for measuring without a mouse.
*   **What it does:** Instead of sending a simple electrical signal, this mode makes the Teensy act as a **real USB mouse** and sends a standard mouse click to the PC. The timer starts the instant the USB packet is sent. This measures the *entire* latency pipeline, including USB polling, OS processing, game engine, and display response time. This mode relies on the 8kHz polling patch to function at its best.
*   **Use case:** Measuring latency without a mouse being hooked in, will still measure all the usual things since the Teensy 4.1 acts as a 8kHz mouse.

---

## Verifying Your Polling Rate

To ensure the 8kHz patch is working and your device is performing optimally in Direct Mode, you can use the included polling rate tester.

1.  Open the `polling_tester.cpp.txt` file and copy its entire contents.
2.  Paste the contents into `main.cpp`, completely replacing the existing code.
3.  Upload the new code to your Teensy.
4.  The Teensy will now act as a mouse, moving the cursor in a smooth circle.
5.  Use a polling rate test utility like the excellent **[HamsterWheel Mouse Tester](https://github.com/szabodanika/HamsterWheel)** to verify that the polling rate is stable at ~8000 Hz.
6.  Once verified, restore the original code to `main.cpp` and re-upload.