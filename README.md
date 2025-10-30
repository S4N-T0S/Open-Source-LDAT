# Open-Source-LDAT Teensy 4.1 (Latency Analyser)
<p align="center">
  <img src="https://github.com/S4N-T0S/Open-Source-LDAT/blob/main/readme_media/Open-Source-LDAT_S4N-T0S_Showcase.gif">
</p>
This project provides the firmware for a powerful, DIY "click-to-photon" system latency tester, similar in function to commercial devices like NVIDIA's LDAT. Built on the high-performance Teensy 4.1, this tool precisely measures the total time from a mouse click until a corresponding visual change appears on your screen.

Designed for accuracy, it uses Teensy-specific optimizations for high-speed I/O. The device features an OLED display for real-time statistics and is controlled by a simple, intuitive single button.

## Key Features

*   **High-Speed Measurement:** Uses the Teensy `ADC` library and `digitalWriteFast` for minimal I/O overhead and rapid sensor readings.
*   **Multiple Testing Modes:** Includes a general-purpose automatic mode and specialized modes for use with controlled testing software.
*   **True 8kHz Polling:** A custom build script temporarily patches the Teensy core to enable a true 8000 Hz USB polling rate for maximum accuracy in Direct Mode.
*   **On-Device Stats:** The OLED screen displays live latency data, including the last, average, minimum, and maximum measurements, plus a run counter.
*   **Hardware Diagnostics:** A comprehensive self-check runs on boot to verify all components are functioning correctly.
*   **Simple One-Button UI:** A clever, multi-level hold system allows for full device control with just a single push button.

---

## Hardware Requirements

This project requires soldering and basic electronics knowledge.

*   **Microcontroller:** Teensy 4.1
*   **Light Sensor:** TEMT6000 Ambient Light Sensor
*   **Display:** 128x64 I2C SSD1306 OLED Display
*   **Input:** Momentary push button
*   **Transistor:** BC547A NPN Transistor (or similar)
*   **Resistor:** 220 Ohm Resistor
*   **Host Mouse:** An old or spare mouse, wired into the left click switch.

![Wiring Diagram](https://github.com/S4N-T0S/Open-Source-LDAT/blob/main/readme_media/Open-Source-LDAT_S4N-T0S_Wiring.jpg)

---

## Software & Environment

This firmware is built using **Visual Studio Code** with the **PlatformIO** extension.

### Why PlatformIO?

PlatformIO simplifies the development process by automatically managing toolchains, libraries, and board configurations. Crucially, it allows us to run the `8kHz_polling.py` script during the build. This script temporarily patches the Teensy core files to change the USB mouse polling interval to 125µs (8000 Hz), which is essential for accurate latency measurement in Direct Mode. The script safely backs up and restores the original file after each compilation.

---

## Getting Started: Installation & Configuration

### Step 1: IMPORTANT - Configure Your `config.h`

Before compiling, you **must** customize the `include/config.h` file. This is the central configuration for the project.

1.  **Pinout:** Ensure the pin definitions (`PIN_BUTTON`, `PIN_SEND_CLICK`, etc.) match your physical wiring.
2.  **Thresholds & Tuning:** The default sensor values will likely need tuning for your specific hardware and environment.
    *   `LIGHT_SENSOR_THRESHOLD` / `DARK_SENSOR_THRESHOLD`: Use the **LSensor Debug** mode to fine-tune these values. Place the sensor on your monitor and observe the live readings for a fully white and a fully black screen, then set your thresholds accordingly.
    *   **Note for OLED Monitor Users:** The default values were calibrated on an OLED display where black levels are zero. If you are using an LCD/LED panel, you will need to adjust these. Additionally, disable any auto-dimming features (like LEA or ABL), as they can progressively dim the white test area and interfere with sensor readings.
    *   `MOUSE_PRESENCE_MIN_ADC_VALUE` / `MOUSE_STABILITY_THRESHOLD_ADC`: These values confirm a mouse is connected. Use the **Mouse Debug** mode to see the live reading and adjust if needed.
3.  **Click Timing:**
    *   `MOUSE_CLICK_HOLD_MICROS`: This value dictates how long the click signal is held in UE4 modes. Tune it based on your system's polling rate to ensure clicks are reliably detected. The comments in the file provide safe starting points.

### Step 2: Compile and Upload

1.  Open the project folder in Visual Studio Code with PlatformIO installed.
2.  Connect your Teensy 4.1.
3.  Click the **Upload** button (right-arrow icon) in the PlatformIO toolbar. PlatformIO will handle everything else.

---
## Test Environment Setup (Choosing a Latency Marker)

To measure latency in games, you need an on-screen visual indicator that responds to your clicks. Your choice of tool depends on your graphics card.

### For NVIDIA Users (Recommended)

The best method is to use the **NVIDIA Reflex Latency Flash Indicator**, which is supported by many competitive games. This feature must be enabled within the game's graphics settings.

For games that don't offer a menu option, a `.bat` script that can force the setting on or off is included in the `/scripts` folder of this project, thanks to GitHub user **@FR33THY**.

### For AMD & All Other Users

Your best option is to use the **RTSS FCAT Latency Marker**.

1.  **Download and install RivaTuner Statistics Server (RTSS)** from the official [Guru3D website](https://www.guru3d.com/download/rtss-rivatuner-statistics-server-download/).
2.  Open RTSS and click the **Setup** button in the bottom-left.
3.  Find the "Enable frame color indicator" option, tick it and select **"Latency marker"** from the dropdown menu.
4.  A black square will now appear in your game, which turns white when you click. To customize it, you can edit the `Global` profile file located at:
    `C:\Program Files (x86)\RivaTuner Statistics Server\Profiles`
    *   **Size:** Change `FrameColorBarWidth` to a larger value (e.g., `128`).
    *   **Position:** Change `FrameColorBarPos` to `8` to place it in the bottom right, which is ideal for how displays typically render frames.
    *   **Confirm settings:** Ensure `EnableFrameColorBar` is `1` and `FrameColorBarMode` is `5`.

---

## How to Use the Device

### The One-Button UI

The device is controlled with a single button using different press durations:

*   **Short Press (Click):** Cycles through menu options.
*   **Long Press (Select):** Hold for ~0.75 seconds. A "SELECT" progress bar will fill. Releasing selects the highlighted option.
*   **Debug Press (Debug Menu):** Hold for ~1.25 seconds. A "DEBUG" bar will fill, taking you to the hardware diagnostic tools.
*   **Reset Press (Reset):** Hold for ~1.75 seconds. A "RESET" bar will fill. Releasing will perform a software reset of the device.

### On-Boot: The Setup Screen

On startup, the device enters `SETUP MODE` and performs a hardware diagnostic. It will check the Monitor, Sensor, and Mouse connections. If a check fails, the device will halt on a debug screen for that component and the onboard LED will blink. If all checks pass, it will prompt you to hold the button to continue.

---

## Operating Modes Explained

### 1. Automatic Mode

A general-purpose test that measures the latency of your display panel.
*   **How it works:** The device sends a click signal via its output pin, starts a timer, and waits for the sensor to detect the screen changing from dark to light.
*   **Use case:** Great for getting a baseline system reading or testing a monitor's raw response time without involving the USB stack.

### 2. Auto UE4 Aperture Mode

Designed specifically for the **Aperture Grille Latency Tester** software. This is the recommended mode for initial setup and calibration.
*   **Website:** [Aperture Grille Software](https://www.aperturegrille.com/software/)
*   **How it works:** Similar to Automatic Mode, but tailored for the black/white toggle in the Aperture Grille app. It runs a smart sync and warm-up routine to ensure measurements are synchronized and repeatable.
*   **Use case:** Provides a controlled environment for comparing settings like VSync, frame caps, or driver options.

### 3. Direct UE4 Aperture Mode

Measures the entire latency pipeline, from USB input to photon output.
*   **How it works:** The Teensy acts as a real 8kHz USB mouse and sends a standard click to the PC. The timer starts the instant the USB packet is sent. This mode relies on the 8kHz polling patch for its high accuracy.
*   **Use case:** Measures the complete end-to-end latency of your system, without needing to wire a mouse.

---

## Verifying Your Polling Rate

To confirm the 8kHz patch is working correctly for Direct Mode:
1.  Open `polling_tester.cpp` and copy its contents into `main.cpp`, replacing everything.
2.  Upload the new code to your Teensy. It will now act as a mouse moving in a circle.
3.  Use a utility like **[HamsterWheel Mouse Tester](https://github.com/szabodanika/HamsterWheel/releases/tag/0.4)** to verify a stable polling rate of ~8000 Hz.
4.  Once done, restore the original code to `main.cpp` and re-upload.

---
## A Note on Measurement Accuracy

Understanding the nuances of latency testing is key to interpreting your results.

The main challenge in measuring click-to-photon latency is that a game's engine only polls for new inputs once per frame. This creates an inherent margin of error. For example, if your click occurs right after the game has checked for input, your action won't be processed until the *next* frame. This means your measured latency can vary by up to one full frametime (e.g., 16.7ms at 60 FPS). You might get a "perfect" click that is processed immediately, or an "unlucky" one that has to wait for the next cycle.

Tools like the **RTSS Latency Marker** are also affected by this, with an additional caveat: the marker is an overlay injected by an external program. It's possible for RTSS to draw its white box on a frame where the game has *received* the input but hasn't yet rendered its effect. This can lead to slightly lower latency readings compared to in-engine tools like NVIDIA's Reflex Flash, which are more tightly integrated with the render pipeline. For this reason, RTSS is also not suitable for measuring latency with Frame Generation technologies, as the marker may be displayed on an interpolated "fake" frame.