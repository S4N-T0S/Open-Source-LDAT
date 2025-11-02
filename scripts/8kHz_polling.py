"""
    Open-Source-LDAT - Latency Detection and Analysis Tool
    Copyright (C) 2025 S4N-T
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later versio
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more detail
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 """
# This script uses the "Direct Inject" method to guarantee the patch is applied.
# It works by:
# 1. Finding the original usb_desc.h in the framework-arduinoteensy package.
# 2. Creating a backup of the original file (usb_desc.h.bak).
# 3. Overwriting the original usb_desc.h with our patched 8kHz version.
# 4. After the compilation is complete, it automatically restores the backup.
# This is a safe and robust way to bypass PlatformIO build system quirks.

import os
import shutil
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()

print("PlatformIO: Running 8kHz patch script (Direct Inject method)...")

# --- Get path to the original header file ---
framework_dir = platform.get_package_dir("framework-arduinoteensy")
core_dir = os.path.join(framework_dir, "cores", "teensy4")
original_header_path = os.path.join(core_dir, "usb_desc.h")
backup_header_path = os.path.join(core_dir, "usb_desc.h.bak")

# --- Safety Check and Restore ---
# If a backup file exists, it means the last build might have failed.
# Restore it now to ensure we start from a clean slate.
if os.path.exists(backup_header_path):
    print("PlatformIO: Found old backup. Restoring original file before patching.")
    shutil.copy(backup_header_path, original_header_path)
    os.remove(backup_header_path)

# --- Create a backup of the original file ---
try:
    shutil.copy(original_header_path, backup_header_path)
    print(f"PlatformIO: Backup of original usb_desc.h created.")
except IOError as e:
    print(f"Error: Could not create backup file: {e}")
    exit(1)

# --- Read, Patch, and Overwrite the Original File ---
try:
    with open(original_header_path, "r") as f:
        content = f.read()

    # Define the exact lines to find and replace
    original_mouse_interval = "#define MOUSE_INTERVAL        2"
    modified_mouse_interval = "#define MOUSE_INTERVAL        1" # 1 = 8kHz

    if original_mouse_interval in content:
        content = content.replace(original_mouse_interval, modified_mouse_interval)
        with open(original_header_path, "w") as f:
            f.write(content)
        print("PlatformIO: Original usb_desc.h has been patched for 8kHz.")
    else:
        print("PlatformIO: MOUSE_INTERVAL not found or already patched. Skipping.")

except IOError as e:
    print(f"Error: Could not read/write original header file: {e}")
    # Restore from backup on failure
    shutil.copy(backup_header_path, original_header_path)
    os.remove(backup_header_path)
    exit(1)

# --- Define the Cleanup Action ---
# This function will be called after the build is done (or fails).
def restore_backup(source, target, env):
    print("PlatformIO: Build finished. Restoring original usb_desc.h from backup...")
    if os.path.exists(backup_header_path):
        shutil.copy(backup_header_path, original_header_path)
        os.remove(backup_header_path)
        print("PlatformIO: Original file restored.")
    else:
        print("PlatformIO: Warning: Backup file not found, cannot restore.")

# Register the cleanup action to run after building the final firmware file.
env.AddPostAction(".pio/build/teensy41/firmware.elf", restore_backup)