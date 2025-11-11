import queue
import sys
import time
import string
import threading
import subprocess

try:
    import RPi.GPIO as GPIO
except ImportError:
    print("RPi.GPIO not available. This script requires Raspberry Pi.")
    sys.exit(1)

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
# GPIO pins (BCM numbering)
LED_PIN = 17
VIBRATION_PIN = 18
TRIGGER_PIN = 27  # Input from ESP32 GPIO 7

# Customize phrases (10 default fraudulent phrases — change as needed)
PHRASES = [
    "3-digit code",
    "4-digit number",
    "6-digit code",
    "8-digit security",
    "access",
    "access key",
    "account",
    "account balance",
    "account details",
    "account holder",
    "account number",
    "add new party",
    "agent",
    "altcoin",
    "amazon voucher",
    "atm",
    "atm pin",
    "authenticate",
    "authentication word",
    "authorize standing order",
    "auto payment",
    "automatic debit",
    "balance",
    "bank",
    "bank account",
    "bank code",
    "bank details",
    "bank identifier",
    "banking information",
    "beneficiary",
    "bic",
    "bic code",
    "bills",
    "bit coins",
    "bitcoin",
    "branch code",
    "btc",
    "bucks",
    "capital",
    "card details",
    "card digits",
    "card end date",
    "card expires on",
    "card number",
    "card validity",
    "card verification code",
    "card verification value",
    "carry across",
    "cash",
    "cash machine",
    "cash out",
    "cash pickup",
    "cashpoint",
    "certificate",
    "channel",
    "charge",
    "check id",
    "claim",
    "clear",
    "clear balance",
    "click refund link",
    "code",
    "code we have sent",
    "codeword",
    "coins",
    "collect",
    "collector",
    "compensation",
    "compensation will be credited",
    "confirm identity",
    "connect",
    "coupon",
    "courier",
    "cover",
    "credential",
    "credit",
    "credit back",
    "credit card number",
    "crypto",
    "cryptocurrency",
    "currency",
    "customer id",
    "cvc",
    "cvv",
    "damages",
    "deal",
    "delivery boy",
    "deposit",
    "digital currency",
    "direct debit",
    "disbursement",
    "discount code",
    "dispatch",
    "dispenser",
    "displace",
    "divert",
    "do the needful and transfer",
    "dosh",
    "dough",
    "draw",
    "draw out",
    "e-cash",
    "end user",
    "enter",
    "enter cvc number",
    "enter full card number",
    "enter password",
    "envelope",
    "execution",
    "expenditure",
    "expiration date",
    "expiry date",
    "express money service",
    "extract",
    "fee",
    "fetch",
    "fiver",
    "float",
    "folder",
    "folding money",
    "follow secure verification link",
    "forward",
    "funds",
    "gather",
    "gift card",
    "go inside",
    "go to nearby atm",
    "google play card",
    "grand",
    "greenbacks",
    "hard cash",
    "holdings",
    "hole in the wall",
    "iban",
    "iban number",
    "identification number",
    "international bank account number",
    "international number",
    "international remittance",
    "itunes card",
    "kindly accept compensation",
    "kindly claim refund",
    "kindly enter otp",
    "kindly enter passcode",
    "kindly move funds to safe account",
    "kindly pay",
    "kindly provide credit card number",
    "kindly provide cvv",
    "kindly provide expiry date",
    "kindly provide memorable word",
    "kindly set up direct debit",
    "kindly share password",
    "kindly transfer",
    "kindly unlock account",
    "kindly verify",
    "kiosk",
    "kitty",
    "kyc check",
    "legal tender",
    "liquidity",
    "lodge",
    "log in",
    "login",
    "login key",
    "logon",
    "make deposit",
    "make remittance",
    "man will collect",
    "memorable code",
    "memorable number",
    "memorable word",
    "messenger",
    "money",
    "money back",
    "money wiring",
    "moneygram",
    "move",
    "move amount",
    "nest egg",
    "new payee setup",
    "notes",
    "offset",
    "one-time code",
    "one-time password",
    "operation",
    "otp",
    "otp / 2fa",
    "outlay",
    "package",
    "packet",
    "passcode",
    "password",
    "pay",
    "pay in",
    "payee",
    "payment",
    "payment link",
    "personal code",
    "phrase",
    "pickup",
    "pickup service",
    "pin",
    "place",
    "please login",
    "please withdraw",
    "post",
    "posting",
    "pouch",
    "prepaid card",
    "prepaid code",
    "prepaid debit",
    "prepaid top-up",
    "prepaid voucher",
    "proceed with payment",
    "profile",
    "promo code",
    "protected account",
    "prove identity",
    "provide 2fa code",
    "pull",
    "push code",
    "put in",
    "quid",
    "re-authenticate",
    "reactivate immediately",
    "readies",
    "reallocate",
    "receiver",
    "recipient",
    "record",
    "recurring transfer",
    "refund",
    "refund link",
    "refund will be processed",
    "reimbursement",
    "reinstate",
    "reloadable card",
    "relocate",
    "remaining funds",
    "remit",
    "remit funds",
    "remit urgently",
    "remove",
    "reopen account",
    "repayment",
    "reposition",
    "reserves",
    "reset access",
    "resources",
    "retrieve",
    "routing code",
    "routing number",
    "runner",
    "safe account",
    "safety account",
    "scheduled payment",
    "scratch card",
    "sealed cover",
    "secondary code",
    "secret number",
    "secret word",
    "secure account",
    "secure link",
    "security code",
    "security number",
    "security word",
    "send",
    "send by wire",
    "send only",
    "send over",
    "service account",
    "settlement",
    "share memorable code",
    "share the verification code",
    "share your passcode",
    "shift",
    "shift funds",
    "ship",
    "shopping card",
    "sign in",
    "sms code",
    "sort code",
    "specie",
    "statement balance",
    "steam card",
    "store card",
    "store credit",
    "stored-value card",
    "sum",
    "sweep",
    "sweep across",
    "swift",
    "swift code",
    "take",
    "take out",
    "take out the amount",
    "telegraphic transfer",
    "teller machine",
    "tenner",
    "terminal",
    "token",
    "total",
    "transaction",
    "transfer",
    "transfer service",
    "transfer to secure account",
    "transmit",
    "transport",
    "tt",
    "two-factor code",
    "unblock your account",
    "unfreeze account",
    "unique number",
    "unlock account",
    "user id",
    "validate",
    "verification code",
    "verification page",
    "verify identity",
    "virtual money",
    "voucher",
    "wealth",
    "western union",
    "wire",
    "wire funds",
    "wire immediately",
    "wire money",
    "wire transfer",
    "withdraw",
    "wrapper",
    "wu",
    "your good account"
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
    On first detection: turn on LED.
    On second detection: vibrate.
    After second, reset cycle.
    """
    with detection_lock:
        detection_counts[phrase] += 1
        count = detection_counts[phrase]

    if count == 1:
        print(f"\n FIRST DETECTED: \"{phrase}\"")
        GPIO.output(LED_PIN, GPIO.HIGH)  # Turn on LED
        print("LED turned ON")
    elif count == 2:
        print(f"\n SECOND DETECTED: \"{phrase}\"")
        GPIO.output(VIBRATION_PIN, GPIO.HIGH)  # Vibrate
        print("Vibration activated")
        time.sleep(1)  # Vibrate for 1 second
        GPIO.output(VIBRATION_PIN, GPIO.LOW)
        print("Vibration stopped")
        # Reset cycle after second detection
        detection_counts[phrase] = 0
        GPIO.output(LED_PIN, GPIO.LOW)  # Turn off LED
        print("Cycle reset, LED turned OFF")
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
    # GPIO setup
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(LED_PIN, GPIO.OUT)
    GPIO.setup(VIBRATION_PIN, GPIO.OUT)
    GPIO.setup(TRIGGER_PIN, GPIO.IN, pull_up_down=GPIO.PUD_DOWN)
    # Wait for GPIO trigger to activate silence monitoring
    if GPIO:
        print("Waiting for GPIO trigger to activate silence monitoring...")
        while GPIO.input(TRIGGER_PIN) == GPIO.LOW:
            time.sleep(0.1)
        print("GPIO trigger received. Starting silence monitoring...")

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
