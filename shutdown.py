#!/usr/bin/env python3
"""
SafePhrase Raspberry Pi: Auto-shutdown on prolonged silence.

Listens to the default ALSA input device and measures loudness.
If there is continuous silence for MIN_SILENCE_SECONDS, it issues a graceful shutdown.

Dependencies:
  pip install pyaudio numpy
"""

import os
import sys
import time
import math
import signal
import argparse
import subprocess
from contextlib import contextmanager

try:
    import pyaudio
    import numpy as np
except Exception as e:
    print("Missing dependency. Please 'pip install pyaudio numpy'")
    raise

SAMPLE_RATE = 16000
FRAME_DURATION_MS = 30
CHANNELS = 1
BYTES_PER_SAMPLE = 2
FRAME_SIZE = int(SAMPLE_RATE * FRAME_DURATION_MS / 1000)
CALIBRATION_SECONDS = 3.0
MIN_SILENCE_SECONDS = float(os.getenv("SP_MIN_SILENCE_SECONDS", "30"))
SILENCE_MARGIN_DB = float(os.getenv("SP_SILENCE_MARGIN_DB", "3.0"))
GRACE_PERIOD_AFTER_START = float(os.getenv("SP_GRACE_PERIOD", "20"))
CHECK_INTERVAL = 0.1

def list_devices(pa):
    print("Input devices:")
    for i in range(pa.get_device_count()):
        info = pa.get_device_info_by_index(i)
        if int(info.get("maxInputChannels", 0)) > 0:
            print(f"[{i}] {info.get('name')} | rate={info.get('defaultSampleRate')} | channels={info.get('maxInputChannels')}")

@contextmanager
def audio_stream(device_index=None):
    pa = pyaudio.PyAudio()
    try:
        stream = pa.open(format=pyaudio.paInt16,
                         channels=CHANNELS,
                         rate=SAMPLE_RATE,
                         input=True,
                         input_device_index=device_index,
                         frames_per_buffer=FRAME_SIZE)
        yield pa, stream
    finally:
        try:
            stream.stop_stream(); stream.close()
        except Exception:
            pass
        pa.terminate()

def rms_dbfs(samples_int16: np.ndarray) -> float:
    if samples_int16.size == 0:
        return -120.0
    samples = samples_int16.astype(np.float32) / 32768.0
    rms = np.sqrt(np.mean(np.square(samples)) + 1e-12)
    dbfs = 20.0 * math.log10(rms + 1e-12)
    return float(max(-120.0, dbfs))

def calibrate_ambient(stream, duration_s=CALIBRATION_SECONDS):
    frames_needed = int(duration_s * 1000 / FRAME_DURATION_MS)
    vals = []
    for _ in range(frames_needed):
        data = stream.read(FRAME_SIZE, exception_on_overflow=False)
        arr = np.frombuffer(data, dtype=np.int16)
        vals.append(rms_dbfs(arr))
        time.sleep(0.0)
    median_db = float(np.median(vals)) if vals else -60.0
    silence_threshold = median_db - SILENCE_MARGIN_DB
    silence_threshold = max(-90.0, min(-10.0, silence_threshold))
    print(f"[Calibrated] ambient median={median_db:.1f} dBFS, silence_threshold={silence_threshold:.1f} dBFS")
    return silence_threshold

def main():
    parser = argparse.ArgumentParser(description="Shutdown on prolonged silence")
    parser.add_argument("--device", type=int, default=None, help="ALSA device index")
    parser.add_argument("--list-devices", action="store_true", help="List input devices and exit")
    parser.add_argument("--min-silence", type=float, default=MIN_SILENCE_SECONDS, help="Seconds of continuous silence before shutdown")
    parser.add_argument("--grace", type=float, default=GRACE_PERIOD_AFTER_START, help="Startup grace period seconds")
    args = parser.parse_args()

    if args.list_devices:
        pa = pyaudio.PyAudio()
        list_devices(pa)
        pa.terminate()
        return 0

    device_index = args.device
    env_idx = os.getenv("MICROPHONE_INDEX")
    if device_index is None and env_idx is not None:
        try:
            device_index = int(env_idx)
        except ValueError:
            device_index = None

    shutdown_cmd = ["/sbin/shutdown", "-h", "now"]

    def handle_sigterm(signum, frame):
        print("Received SIGTERM. Exiting.")
        sys.exit(0)

    signal.signal(signal.SIGTERM, handle_sigterm)

    start_time = time.time()
    last_non_silent = time.time()

    with audio_stream(device_index) as (pa, stream):
        threshold_db = calibrate_ambient(stream)

        print(f"Monitoring... min_silence={args.min_silence}s, grace={args.grace}s")
        while True:
            data = stream.read(FRAME_SIZE, exception_on_overflow=False)
            arr = np.frombuffer(data, dtype=np.int16)
            level = rms_dbfs(arr)

            if level > threshold_db:
                last_non_silent = time.time()

            if time.time() - start_time < args.grace:
                time.sleep(CHECK_INTERVAL)
                continue

            silence_duration = time.time() - last_non_silent
            if silence_duration >= args.min_silence:
                print(f"Silence for {silence_duration:.1f}s. Initiating shutdown...")
                try:
                    subprocess.run(["sudo"] + shutdown_cmd, check=False)
                except Exception as e:
                    print(f"Failed to call shutdown: {e}")
                time.sleep(2.0)
                return 0

            time.sleep(CHECK_INTERVAL)

if __name__ == "__main__":
    sys.exit(main())
