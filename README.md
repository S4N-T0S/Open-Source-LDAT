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
4.  **Run Limits:**
    *   `RUN_LIMIT_OPTION_1`, `_2`, `_3`: These variables set the run count options available in the "Select Run Limit" menu. You can change `100`, `300`, `500` to any values you prefer (e.g., `50`, `150`, `1000`).

### Step 2: Compile and Upload

1.  Open the project folder in Visual Studio Code with PlatformIO installed.
2.  Connect your Teensy 4.1.
3.  Click the **Upload** button (right-arrow icon) in the PlatformIO toolbar. PlatformIO will handle everything else.

---
## Test Environment Setup (Choosing a Latency Marker)

To measure latency in games, you need an on-screen visual indicator that responds to your clicks. Your choice of tool depends on your graphics card.

### For NVIDIA Users (Recommended)

The best method is to use the **NVIDIA Reflex Latency Flash Indicator**, which is supported by many competitive games. This feature must be enabled within the game's graphics settings.

For games that don't offer a menu option, a `.bat` script that can force the setting on or off is included in the `/scripts` folder of this project, thanks to GitHub user [@fr33thyfr33thy](https://github.com/fr33thyfr33thy).

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

To verify the Teensy is running at a 8kHz polling rate, use the integrated tester. Hold the button for ~1.25 seconds to enter the **Debug Menu**, then select **Polling Test**. The device will begin moving your mouse cursor in a circle. Use a PC utility like **[HamsterWheel Mouse Tester](https://github.com/szabodanika/HamsterWheel/releases/tag/0.4)** to confirm a stable ~8000 Hz rate.

A single short press stops the test and returns you to the debug menu.

---
## A Note on Measurement Accuracy

Understanding what causes latency and its variability is key to interpreting your results. A game's render time (e.g., 1ms at 1000 FPS) is just one piece of a much larger puzzle. The final number you see is the sum of delays from a long chain of events—the **"click-to-photon" pipeline**—and this device is designed to measure that entire chain.

### The Latency Pipeline

Every time you click, a race against several independent clocks begins. The total delay is the sum of the time spent in each stage:

1.  **Mouse & USB Polling:** The click is processed by the mouse's internal hardware and must then wait to be picked up by the PC. With the **8000Hz** polling rate patch, the PC checks for an update every 0.125ms. This can add anywhere from **0ms to 0.125ms** of random delay, depending on when your click occurs within that tiny polling cycle.

2.  **OS & Game Engine:** The operating system processes the input, and the game engine samples it. The engine typically only checks for input once per frame. This means your input has to wait for the next frame to begin processing, adding a variable delay of **0ms up to one full frametime**. For a game running at 240 FPS, this adds 0ms to 4.167ms of latency.

3.  **Display Refresh (Scanout):** This is the final step where the monitor displays the rendered frame. A 240Hz monitor begins drawing a new image from top-to-bottom every **4.167ms**. If the new frame is rendered just *after* a scanout begins, it must wait in the GPU's buffer for the entire 4.167ms for the next cycle. This adds **0ms to 4.167ms** of random delay.

### Why Min and Max Latency Are So Different

The huge variance between minimum and maximum readings is not an error; it's the expected result of the random alignment of these independent cycles.

Let's use a realistic gaming scenario: 240 FPS on a 240Hz monitor, with an 8kHz mouse.

*   **A "perfectly lucky" click (Minimum Latency):** Your click occurs right before the USB poll, which happens right before the game's input sample, and the frame is rendered just in time for the next monitor refresh. All the variable delays are near zero.
    *   *Example:* `~1.5ms (mouse hardware) + 0.1ms (USB) + 0.5ms (Game) + 0.5ms (Scanout)` = **~2.6ms** (a very low, but plausible reading).

*   **An "unlucky" click (Maximum Latency):** Your click happens *just after* each of these checks, forcing it to wait for the next full cycle at every single step.
    *   *Example:* `~1.5ms (hardware) + 0.125ms (missed 8kHz poll) + 4.167ms (missed game sample) + 4.167ms (missed scanout)` = **~10.0ms**

This demonstrates how, even in a perfectly stable setup, real-world system latency can and will vary significantly. This device accurately captures that entire range, giving you the true picture of your system's performance.

### Notes on Software Markers (RTSS)

Tools like the **RTSS Latency Marker** are also affected by this pipeline. However, there's an additional caveat: the marker is an overlay injected by an external program. It's possible for RTSS to draw its white box on a frame where the game has *received* the input but hasn't yet rendered its effect. This can lead to slightly lower latency readings compared to in-engine tools like NVIDIA's Reflex Flash, which are more tightly integrated with the render pipeline. For this reason, RTSS is also not suitable for measuring latency with Frame Generation technologies, as the marker may be displayed on an interpolated "fake" frame.