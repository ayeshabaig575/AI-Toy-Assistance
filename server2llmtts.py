from fastapi import FastAPI, Request, BackgroundTasks
from fastapi.responses import FileResponse
import whisper
import uuid
import os
import requests
from TTS.api import TTS
import soundfile as sf
import librosa

app = FastAPI()

# -------------------- LOAD MODELS --------------------
print("Loading Whisper model...")
whisper_model = whisper.load_model("base")
print("Whisper ready")

print("Loading Coqui TTS model...")
tts_model = TTS(
    model_name="tts_models/en/ljspeech/tacotron2-DDC",
    progress_bar=False,
    gpu=False
)
print("TTS ready")

# Ollama HTTP API URL
OLLAMA_URL = "http://localhost:11434/api/generate"

# -------------------- STT → LLM → TTS --------------------
@app.post("/stt")
async def stt(request: Request, background_tasks: BackgroundTasks):

    # 1️⃣ Receive WAV bytes from ESP32
    audio_bytes = await request.body()
    wav_file = f"audio_{uuid.uuid4().hex}.wav"
    with open(wav_file, "wb") as f:
        f.write(audio_bytes)

    # 2️⃣ Whisper STT
    try:
        result = whisper_model.transcribe(wav_file)
        user_text = result["text"].strip()
        print("STT:", user_text)
    except Exception as e:
        print("Whisper error:", e)
        user_text = ""
    finally:
        os.remove(wav_file)

    if not user_text:
        return {"stt": "", "llm": "", "tts": ""}

    # 3️⃣ TinyLLaMA via Ollama HTTP API
    try:
        payload = {
            "model": "tinyllama",
            "prompt": user_text,
            "stream": False,
            "options": {
                "num_predict": 80   # limit response length
            }
        }

        response = requests.post(OLLAMA_URL, json=payload, timeout=30)
        response.raise_for_status()
        ai_response = response.json()["response"].replace("\n", " ").strip()
        print("LLM:", ai_response)

    except Exception as e:
        print("LLM error:", e)
        ai_response = "Sorry, I could not process that."

    # 4️⃣ TTS → 16 kHz PCM WAV
    tts_file = f"tts_{uuid.uuid4().hex}.wav"
    try:
        tts_model.tts_to_file(text=ai_response, file_path=tts_file)

        # Resample to 16 kHz mono PCM 16-bit (ESP32 compatible)
        y, _ = librosa.load(tts_file, sr=16000, mono=True)
        sf.write(tts_file, y, 16000, subtype="PCM_16")
        print("TTS saved:", tts_file)

    except Exception as e:
        print("TTS error:", e)
        return {"stt": user_text, "llm": ai_response, "tts": ""}

    # 5️⃣ Send audio to ESP32 and delete after sending
    background_tasks.add_task(os.remove, tts_file)
    return FileResponse(
        tts_file,
        media_type="audio/wav",
        filename="response.wav"
    )

# -------------------- RUN --------------------
# uvicorn server2llmtts:app --host 0.0.0.0 --port 8000
