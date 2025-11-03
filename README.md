# Fraudulent Phrase Detector

This Python script detects multiple fraudulent phrases in real-time using speech recognition. It can operate in online mode (via Google Speech API) or offline mode (via Vosk). When a phrase is detected, it prints notifications for the first and second detections.

## Features

- Detects customizable fraudulent phrases.
- Supports online (Google) and offline (Vosk) speech recognition.
- Configurable matching sensitivity.
- Thread-safe detection counting.
- Microphone calibration and selection.

## Installation

1. Clone or download the repository.
2. Install the required dependencies:

   ```bash
   pip install -r requirements.txt
   ```

   Note: For offline mode, download a Vosk model (e.g., `vosk-model-en-us-0.22-lgraph`) and set the `VOSK_MODEL_PATH` in the script.

## Configuration

Edit the configuration section near the top of `main.py`:

- `PHRASES`: List of phrases to detect (default: 10 common fraudulent phrases).
- `VOSK_MODEL_PATH`: Path to the Vosk model directory (for offline mode).
- `MODE`: "google" (online), "vosk" (offline), or "auto" (try Google, fallback to Vosk).
- `MATCH_THRESHOLD`: Similarity threshold for matching (0.0 to 1.0).
- `PHRASE_TIME_LIMIT`: Seconds to listen per attempt.
- `CALIBRATION_DURATION`: Seconds for microphone calibration.
- `MIC_DEVICE_INDEX`: Microphone index (None for default; use `list_microphones()` to find indices).

## Usage

Run the script:

```bash
python main.py
```

The script will start listening and print detected phrases. It will notify on the first and second detections of each phrase.

To list available microphones, uncomment the `list_microphones()` call in `main()`.

## Requirements

- Python 3.x
- Internet connection for online mode
- Vosk model for offline mode

## Notes

- For offline mode, ensure the Vosk model is downloaded and the path is correct.
- Adjust microphone settings if using a USB mic.
- The script uses fuzzy matching for phrase detection.
- . $HOME/esp/esp-idf/export.sh
- cd ~/safe_phrase/wake/components/esp-adf/components/esp-sr/model/multinet_model/fst
- sudo nano commands_en.txt