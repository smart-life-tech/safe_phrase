"""
fraud_phrase_detector.py

Detect multiple fraudulent phrases (online with Google Speech API or offline with Vosk).
Prints:
  FIRST DETECTED: <phrase>
  SECOND DETECTED: <phrase>

Configuration near the top of the file.
"""

import queue
import sys
import time
import string
import threading

import speech_recognition as sr
from difflib import SequenceMatcher

# For Vosk offline
try:
    from vosk import Model, KaldiRecognizer
    import sounddevice as sd
    import json
    VOSK_AVAILABLE = True
except Exception:
    VOSK_AVAILABLE = False

# ---------------- CONFIG ----------------
# Customize phrases (10 default fraudulent phrases — change as needed)
PHRASES = [
    "i lost my card",
    "i forgot my password",
    "send money now",
    "authorize a transfer",
    "urgent payment required",
    "confirm your code",
    "give me your account",
    "transfer all funds",
    "wire money now",
    "cancel the block"
]

# Path to vosk model directory (set if using offline)
VOSK_MODEL_PATH = "vosk-model-en-us-0.22-lgraph"  # adjust as needed

# Mode: "google", "vosk", or "auto"
MODE = "auto"  # "google" = online, "vosk" = offline, "auto" = try google then vosk

# Matching sensitivity [0.0 - 1.0]. Higher => stricter match
MATCH_THRESHOLD = 0.70

# Audio chunk / phrase capture settings
PHRASE_TIME_LIMIT = 4.0  # seconds to listen per attempt
CALIBRATION_DURATION = 2.0  # seconds to adjust ambient noise

# Microphone: None means default. If you want a specific USB mic, set index after listing names.
MIC_DEVICE_INDEX = None  # e.g., 2 for a USB mic (see list below)
# ----------------------------------------

# Internal state
detection_counts = {p: 0 for p in PHRASES}
detection_lock = threading.Lock()


def list_microphones():
    names = sr.Microphone.list_microphone_names()
    print("Available microphones:")
    for i, n in enumerate(names):
        print(f"  {i}: {n}")
    return names


def normalize_text(s: str) -> str:
    s = s.lower()
    # remove punctuation
    s = s.translate(str.maketrans("", "", string.punctuation))
    # collapse multiple spaces
    s = " ".join(s.split())
    return s


def best_match(recognized: str, phrases, threshold=MATCH_THRESHOLD):
    recognized_n = normalize_text(recognized)
    best = None
    best_score = 0.0
    for p in phrases:
        p_n = normalize_text(p)
        # two measures: sequence similarity and token overlap
        seq = SequenceMatcher(None, p_n, recognized_n).ratio()
        # token overlap: fraction of shared words
        p_tokens = set(p_n.split())
        r_tokens = set(recognized_n.split())
        token_overlap = (len(p_tokens & r_tokens) / max(1, len(p_tokens)))
        # combine scores giving more weight to sequence similarity
        score = (0.7 * seq) + (0.3 * token_overlap)
        if score > best_score:
            best_score = score
            best = p
    return best, best_score


def handle_detection(phrase):
    """
    Print FIRST/SECOND detection messages and update counters thread-safely.
    """
    with detection_lock:
        detection_counts[phrase] += 1
        count = detection_counts[phrase]

    if count == 1:
        print(f"\n FIRST DETECTED: \"{phrase}\"")
    elif count == 2:
        print(f"\n SECOND DETECTED: \"{phrase}\"")
    else:
        print(f"\nDetected \"{phrase}\" ({count} times)")


# ---------------- Online (Google) backend using SpeechRecognition ----------------
def run_google_backend():
    r = sr.Recognizer()
    mic = sr.Microphone(device_index=MIC_DEVICE_INDEX)

    print("Using Google Speech API (online). Calibrating microphone...")
    with mic as source:
        r.adjust_for_ambient_noise(source, duration=CALIBRATION_DURATION)
    print("Calibration complete. Listening...")

    while True:
        with mic as source:
            print("\nListening (online)...")
            try:
                audio = r.listen(source, phrase_time_limit=PHRASE_TIME_LIMIT)
            except Exception as e:
                print("Microphone listening error:", e)
                continue

        try:
            text = r.recognize_google(audio)
            print("Recognized (google):", text)
            best, score = best_match(text, PHRASES)
            if score >= MATCH_THRESHOLD:
                print(f"Match: \"{best}\" (score={score:.2f})")
                handle_detection(best)
            else:
                print(f"No match (best='{best}', score={score:.2f})")
        except sr.UnknownValueError:
            print("Could not understand audio (online).")
        except sr.RequestError as e:
            print("Online recognition error (RequestError):", e)
            raise e  # caller can fall back to offline if in auto mode
        except Exception as e:
            print("Unexpected error (online):", e)


# ---------------- Offline (Vosk) backend using sounddevice + vosk ----------------
def run_vosk_backend():
    if not VOSK_AVAILABLE:
        raise RuntimeError(
            "Vosk or sounddevice not installed. Install vosk, sounddevice, numpy and download a model."
        )

    print("Using Vosk (offline). Loading model...")
    model = Model(VOSK_MODEL_PATH)
    # sampling rate: choose one supported by your device; 16000 is common for Vosk small models
    samplerate = 16000

    # Query default input device samplerate if desired
    try:
        default_info = sd.query_devices(None, 'input')
        if default_info and 'default_samplerate' in default_info:
            samplerate = int(default_info['default_samplerate'])
    except Exception:
        pass

    rec = KaldiRecognizer(model, samplerate)
    rec.SetWords(True)

    print(f"Vosk model loaded. Listening (samplerate={samplerate})...")
    # We'll stream small chunks and aggregate recognized text per phrase_time_limit
    q = queue.Queue()

    def callback(indata, frames, time_info, status):
        # indata: numpy array of shape (frames, channels), int16 or float32 depending
        if status:
            print("Sounddevice status:", status, file=sys.stderr)
        q.put(bytes(indata))

    try:
        with sd.RawInputStream(samplerate=samplerate, blocksize=8000, dtype='int16',
                               channels=1, callback=callback):
            print("Calibrating (sleeping a short time to stabilize)...")
            time.sleep(0.5)
            last_chunk_time = time.time()
            buffer = b""
            while True:
                try:
                    data = q.get(timeout=PHRASE_TIME_LIMIT)
                    buffer += data
                    # feed to recognizer
                    if rec.AcceptWaveform(data):
                        res = rec.Result()
                        j = json.loads(res)
                        text = j.get("text", "")
                        if text:
                            print("Recognized (vosk):", text)
                            best, score = best_match(text, PHRASES)
                            if score >= MATCH_THRESHOLD:
                                print(f"Match: \"{best}\" (score={score:.2f})")
                                handle_detection(best)
                            else:
                                print(f"No match (best='{best}', score={score:.2f})")
                            # reset buffer
                            buffer = b""
                    else:
                        # partial result: may ignore or print
                        pass
                except queue.Empty:
                    # time slice done; flush partial
                    # final_result = rec.FinalResult()
                    final = rec.FinalResult()
                    j = json.loads(final)
                    text = j.get("text", "")
                    if text:
                        print("Recognized (vosk final):", text)
                        best, score = best_match(text, PHRASES)
                        if score >= MATCH_THRESHOLD:
                            print(f"Match: \"{best}\" (score={score:.2f})")
                            handle_detection(best)
                        else:
                            print(f"No match (best='{best}', score={score:.2f})")
                    # reset recognizer state
                    rec = KaldiRecognizer(model, samplerate)
                    rec.SetWords(True)
                    buffer = b""

    except KeyboardInterrupt:
        print("Stopped by user (KeyboardInterrupt).")
    except Exception as e:
        print("Vosk backend error:", e)
        raise e


# ---------------- Coordinator / Main ----------------
def main():
    print("Fraudulent Phrase Detector")
    print("=========================")
    print(f"Mode: {MODE}")
    print("Phrases to detect:")
    for p in PHRASES:
        print(" -", p)
    print("\nTip: call list_microphones() near the top of file to find your USB mic index and set MIC_DEVICE_INDEX.\n")

    # Helpful microphone listing
    # Uncomment to show available devices then exit:
    # list_microphones()
    # return

    # Run according to mode
    if MODE == "google":
        try:
            run_google_backend()
        except Exception as e:
            print("Google backend failed:", e)
    elif MODE == "vosk":
        try:
            run_vosk_backend()
        except Exception as e:
            print("Vosk backend failed:", e)
    elif MODE == "auto":
        # Try Google first; on failure switch to Vosk (if installed)
        try:
            run_google_backend()
        except Exception as e:
            print("Google backend error. Falling back to Vosk (if available)...\n", e)
            if VOSK_AVAILABLE:
                try:
                    run_vosk_backend()
                except Exception as e2:
                    print("Vosk also failed:", e2)
            else:
                print("Vosk not available. Install vosk and sounddevice for offline detection.")
    else:
        print("Unknown MODE:", MODE)


if __name__ == "__main__":
    main()
