from fastapi import FastAPI, Request
import whisper
import uuid
import os

app = FastAPI()

print("Loading Whisper model...")
model = whisper.load_model("base")   # use "small" later if you want
print("Whisper ready")

@app.post("/stt")
async def stt(request: Request):
    # 1️⃣ Read raw WAV bytes
    audio_bytes = await request.body()

    # DEBUG
    print("Received bytes:", len(audio_bytes))

    # 2️⃣ Save to temp WAV file
    filename = f"audio_{uuid.uuid4().hex}.wav"
    with open(filename, "wb") as f:
        f.write(audio_bytes)

    print("Saved file:", filename)

    # 3️⃣ Transcribe
    try:
        result = model.transcribe(filename)
        text = result["text"]
        print("TRANSCRIBED:", text)
    except Exception as e:
        print("Whisper error:", e)
        text = ""

    # 4️⃣ Cleanup
    os.remove(filename)

    # 5️⃣ Return text
    return {"text": text}


