from fastapi import FastAPI, Request
import whisper
import uuid
import os
import subprocess

app = FastAPI()

print("Loading Whisper model...")
whisper_model = whisper.load_model("base")
print("Whisper ready")

@app.post("/stt")
async def stt(request: Request):

    # 1️⃣ Receive WAV bytes
    audio_bytes = await request.body()
    print("Received bytes:", len(audio_bytes))

    # 2️⃣ Save temp WAV
    wav_file = f"audio_{uuid.uuid4().hex}.wav"
    with open(wav_file, "wb") as f:
        f.write(audio_bytes)

    # 3️⃣ Whisper STT
    try:
        stt_result = whisper_model.transcribe(wav_file)
        user_text = stt_result["text"].strip()
        print("TRANSCRIBED:", user_text)
    except Exception as e:
        print("Whisper error:", e)
        user_text = ""

    os.remove(wav_file)

    if not user_text:
        return {"stt": "", "llm": ""}

    # 4️⃣ Ollama LLaMA (FIXED)
    try:
        llama_process = subprocess.run(
            ["ollama", "run", "llama3", user_text],
            capture_output=True,
            encoding="utf-8",   # ✅ FIX
            errors="ignore",    # ✅ FIX
            timeout=60
        )
        ai_response = llama_process.stdout.strip()
        print("LLM RESPONSE:", ai_response)

    except Exception as e:
        print("LLM error:", e)
        ai_response = ""

    # 5️⃣ Return response
    return {
        "stt": user_text,
        "llm": ai_response
    }
