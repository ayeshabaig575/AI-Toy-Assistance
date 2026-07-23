from fastapi import FastAPI, Request
from fastapi.responses import StreamingResponse
import google.generativeai as genai
from google.cloud import speech, texttospeech
import io

app = FastAPI()

# ---------- Gemini (FREE) ----------
genai.configure(api_key="AIzaSyCihyDhdDsh2-i8-cSvwufexreBBXD1P3Y")  # your Gemini API key

# ---------- Google Cloud Clients ----------
stt_client = speech.SpeechClient()
tts_client = texttospeech.TextToSpeechClient()


@app.post("/stt")
async def speech_pipeline(request: Request):
    # 1️⃣ Receive WAV audio from ESP32
    wav_data = await request.body()

    # ---------- STT ----------
    audio = speech.RecognitionAudio(content=wav_data)
    config = speech.RecognitionConfig(
        encoding=speech.RecognitionConfig.AudioEncoding.LINEAR16,
        sample_rate_hertz=16000,
        language_code="en-US"
    )
    stt_response = stt_client.recognize(config=config, audio=audio)
    user_text = stt_response.results[0].alternatives[0].transcript
    print("User said:", user_text)

    # ---------- Gemini LLM ----------
    llm_response = genai.chat(
        model="gemini-1.5-turbo",
        messages=[{"role": "user", "content": user_text}]
    )
    reply_text = llm_response.last  # Gemini response text
    print("LLM reply:", reply_text)

    # ---------- Google Cloud TTS ----------
    synthesis_input = texttospeech.SynthesisInput(text=reply_text)
    voice = texttospeech.VoiceSelectionParams(
        language_code="en-US",
        ssml_gender=texttospeech.SsmlVoiceGender.NEUTRAL
    )
    audio_cfg = texttospeech.AudioConfig(
        audio_encoding=texttospeech.AudioEncoding.LINEAR16
    )
    tts_response = tts_client.synthesize_speech(
        input=synthesis_input,
        voice=voice,
        audio_config=audio_cfg
    )

    # Return audio as WAV
    return StreamingResponse(
        io.BytesIO(tts_response.audio_content),
        media_type="audio/wav"
    )
