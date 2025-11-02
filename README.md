# Open-Source-LDAT Teensy 4.1 (Latency Analyser)
<p align="center">
  <img src="https://github.com/S4N-T0S/Open-Source-LDAT/blob/main/readme_media/Open-Source-LDAT_S4N-T0S_Showcase.gif">
</p>
This project provides the firmware for a powerful, DIY "click-to-photon" system latency tester, similar in function to commercial devices like NVIDIA's LDAT. Built on the high-performance Teensy 4.1, this tool precisely measures the total time from a mouse click until a corresponding visual change appears on your screen.

Designed for accuracy, it uses Teensy-specific optimizations for high-speed I/O. The device features an OLED display for real-time statistics and is controlled by a simple, intuitive single button.

## ⚠️ Important: Anticheat Warning
When testing competitive games or any application with an anticheat system, it is strongly recommended to use **Automatic Mode** with the device powered by an external source (e.g., a USB power bank or wall adapter). **Do not connect the device directly to your PC during these tests.**

While the firmware behaves as a standard HID mouse, any unrecognized custom device carries a small risk of being flagged by aggressive anticheat systems. Using Automatic Mode with an external power source completely isolates the device from the PC, ensuring safety.

## Key Features

*   **High-Speed Measurement:** Uses the Teensy `ADC` library and `digitalWriteFast` for minimal I/O overhead and rapid sensor readings.
*   **Multiple Testing Modes:** Includes a general-purpose automatic mode and specialized modes for use with controlled testing software.
*   **True 8kHz Polling:** A custom build script temporarily patches the Teensy core to enable a true 8000 Hz USB polling rate for maximum accuracy in Direct Mode.
*   **On-Device Stats:** The OLED screen displays live latency data, including the last, average, minimum, and maximum measurements, plus a run counter.
*   **SD Card Data Logging:** Automatically save every latency measurement from a test session to a `.csv` file on a microSD card.
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
5.  **SD Card Logging (Optional):**
    > The device can automatically log all latency runs to a microSD card. This feature is **disabled by default**. To enable it, set `ENABLE_SD_LOGGING` to `true`. You can also customize the save directory and the logging interval for 'Unlimited' mode runs in this section.

### Step 2: Compile and Upload

1.  Open the project folder in Visual Studio Code with PlatformIO installed.
2.  Connect your Teensy 4.1.
3.  Click the **Upload** button (right-arrow icon) in the PlatformIO toolbar. PlatformIO will handle everything else.

---
## Test Environment Setup (Choosing a Latency Marker)

To measure latency, the device needs a reliable on-screen visual event (a "latency marker") that appears in direct response to a mouse click. You can use dedicated software tools or even in-game visual effects like a muzzle flash.

### Method 1: Dedicated Software Markers

These tools provide a standardized, high-contrast square that changes color on a click, offering the most reliable and repeatable measurements.

*   **For NVIDIA Users: Reflex Flash Indicator**
    The best method is to use the **NVIDIA Reflex Latency Flash Indicator**, supported by many competitive games. Enable it in the game's graphics settings. For games that don't offer a menu option, a `.bat` script to force the setting is included in the `/scripts` folder of this project, thanks to GitHub user [@fr33thyfr33thy](https://github.com/fr33thyfr33thy).

*   **For AMD & All Other Users: RTSS Latency Marker**
    A "okay" alternative is the **RTSS FCAT Latency Marker**.
    1.  **Download and install RivaTuner Statistics Server (RTSS)** from the official [Guru3D website](https://www.guru3d.com/download/rtss-rivatuner-statistics-server-download/).
    2.  Open RTSS, click **Setup**, find "Enable frame color indicator," tick it, and select **"Latency marker"** from the dropdown.
    3.  A black square that turns white on click will now appear. You can customize its size and position by editing the `Global` profile file located at `C:\Program Files (x86)\RivaTuner Statistics Server\Profiles`.
        *   **Size:** Set `FrameColorBarWidth` to `128` (or larger).
        *   **Position:** Ensure `FrameColorBarPos` is `8` (bottom right).
        *   **Confirm:** Ensure `EnableFrameColorBar` is `1` and `FrameColorBarMode` is `5`.

    > [!WARNING]
    > **Important Note on RTSS Accuracy:** As RTSS is an overlay, its readings may differ from in-engine tools. Crucially, **do not use RTSS with Frame Generation technologies** (e.g., DLSS 3, FSR 3), as this will produce inaccurate results. For a detailed explanation, please see the [*Notes on Software Markers (RTSS)*](?tab=readme-ov-file#notes-on-software-markers-rtss) section at the bottom of this document.

### Method 2: Using In-Game Visuals (Universal)

You can measure latency in **any game** by using an in-game visual effect, such as a **muzzle flash**, as your latency marker. This universal method requires some initial tuning but is extremely powerful.

1.  **Find a suitable spot in your game**, ideally a dark area where the muzzle flash provides a strong contrast.
2.  **Enter the Debug Menu** on your Teensy by holding the external button for ~1.3 seconds, then select **LSensor Debug**.
3.  Place the sensor on your monitor over the area where the flash will appear. The OLED will show a live reading of the light level.
4.  Fire your weapon and observe the peak sensor value when the muzzle flash occurs.
5.  Open the `include/config.h` file and set the `LIGHT_SENSOR_THRESHOLD` to a value that is higher than the dark background but lower than the peak muzzle flash reading (and vice versa to `DARK_SENSOR_THRESHOLD`). This ensures the timer stops only when the flash is detected.
6.  Compile and upload the firmware with your new threshold. The device is now calibrated to use the muzzle flash as its trigger.

This technique allows you to measure latency in situations where dedicated markers are not available, giving you true end-to-end results based on the game engine's actual rendered output.

---

## How to Use the Device

### The One-Button UI

The device is controlled with a single button using different press durations:

*   **Short Press (Click):** Cycles through menu options.
*   **Long Press (Select/Exit/Bypass):** Hold for ~0.8 seconds. A progress bar will fill. Releasing executes the highlighted option.
*   **Debug Press (Debug Menu):** Hold for ~1.3 seconds. A "DEBUG" bar will fill, taking you to the hardware diagnostic tools.
*   **Reset Press (Reset):** Hold for ~1.8 seconds. A "RESET" bar will fill. Releasing will perform a software reset of the device.

### On-Boot: The Setup Screen

On startup, the device enters `SETUP MODE` and performs a hardware diagnostic. It will check the Monitor, Sensor, and Mouse connections. If a check fails, the device will halt on a debug screen for that component and the onboard LED will blink. If all checks pass, it will prompt you to hold the button to continue.

---

## Operating Modes Explained

### 1. Automatic Mode

A general-purpose test that measures latency from the mouse switch to the screen.
*   **How it works:** The device sends a click signal via its output pin (triggering the attached mouse), starts a timer, and waits for the sensor to detect the screen changing from dark to light.
*   **Use case:** This mode is highly versatile. It's excellent for getting a baseline system reading or comparing the hardware latency of different mice. While designed for standardized markers like the RTSS FCAT marker or NVIDIA's Reflex Flash, it can be adapted for other scenarios. By using the **LSensor Debug** tool, you can observe the sensor's raw output and adjust the `LIGHT_SENSOR_THRESHOLD` in the `config.h` file to trigger on other in-game visual events, such as a muzzle flash. This allows for latency measurement in many situations, though it may require careful tuning.

### 2. Auto UE4 Aperture Mode

Designed specifically for the **Aperture Grille Latency Tester** software.
*   **Website:** [Aperture Grille Software](https://www.aperturegrille.com/software/)
*   **How it works:** Similar to Automatic Mode, but tailored for the black/white toggle in the Aperture Grille app. It runs a smart sync and warm-up routine to ensure measurements are synchronized and repeatable.
*   **Use case:** This mode provides a highly stable and repeatable environment ideal for A/B testing the impact of system changes. Use it to accurately measure the effects of different graphics driver settings, OS optimizations, monitor configurations (overdrive, G-Sync/FreeSync), and to compare different physical mice.

### 3. Direct UE4 Aperture Mode

Measures the entire latency pipeline, from USB input to photon output, using the Aperture Grille software.
*   **How it works:** The Teensy acts as a real 8kHz USB mouse and sends a standard click to the PC. The timer starts the instant the USB packet is sent. This mode relies on the 8kHz polling patch for its high accuracy.
*   **Use case:** This is the ultimate test for measuring your complete end-to-end system latency. Because it uses the Teensy as the input device, it's perfect for scientifically testing the latency impact of software settings (drivers, VSync, frame caps, etc.) in a controlled environment, removing the physical mouse as a variable.

---

## Verifying Your Polling Rate

To verify the Teensy is running at a 8kHz polling rate, use the integrated tester. Hold the button for ~1.3 seconds to enter the **Debug Menu**, then select **Polling Test**. The device will begin moving your mouse cursor in a circle. Use a PC utility like **[HamsterWheel Mouse Tester](https://github.com/szabodanika/HamsterWheel/releases/tag/0.4)** to confirm a stable ~8000 Hz rate.

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
    *   *Example:* `~1.5ms (mouse hardware) + 0.1ms (USB poll) + 0.5ms (Game) + 0.5ms (Scanout)` = **~2.6ms** (a very low, but plausible reading).

*   **An "unlucky" click (Maximum Latency):** Your click happens *just after* each of these checks, forcing it to wait for the next full cycle at every single step.
    *   *Example:* `~1.5ms (hardware) + 0.125ms (missed 8kHz poll) + 4.167ms (missed game sample) + 4.167ms (missed scanout)` = **~10.0ms**

This demonstrates how, even in a perfectly stable setup, real-world system latency can and will vary significantly. This device accurately captures that entire range, giving you the true picture of your system's performance.

### Notes on Software Markers (RTSS)

Tools like the **RTSS Latency Marker** are also affected by this pipeline. However, there's an additional caveat: the marker is an overlay injected by an external program. It's possible for RTSS to draw its white box on a frame where the game has *received* the input but hasn't yet rendered its effect. This can lead to slightly lower latency readings compared to in-engine tools like NVIDIA's Reflex Flash, which are more tightly integrated with the render pipeline. For this reason, RTSS is also not suitable for measuring latency with Frame Generation technologies, as the marker may be displayed on an interpolated "fake" frame.