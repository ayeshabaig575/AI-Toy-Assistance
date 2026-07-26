#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <math.h>

// ================= WIFI =================
#define WIFI_SSID "BAIGG"
#define WIFI_PASS "123456789"

// ================= SERVER ===============
#define STT_SERVER_URL "http://192.168.137.1:8000/stt"

// ================= MIC (INMP441) =========
#define MIC_WS   41
#define MIC_SCK  40
#define MIC_SD   42

// ================= SPEAKER (MAX98357A) ===
#define SPK_LRC   5
#define SPK_BCLK  7
#define SPK_DIN   18

// ================= BUTTON / LED =========
#define BTN_PIN  4
#define LED_PIN  21

#define LED_IDLE     15
#define LED_WAITING  16
#define LED_PLAYING  17

// ================= AUDIO =================
#define SAMPLE_RATE     16000
#define RECORD_SECONDS  3
#define MIC_GAIN        0.3f

#define MAX_AUDIO_BYTES 120000   // ~3s WAV in PSRAM

int32_t micBuffer[512];
int16_t pcmBuffer[512];
uint32_t sampleRate = SAMPLE_RATE;
uint8_t *wavBuffer = NULL;
uint32_t wavSize = 0;
bool isRecording = false;

// ================= WAV HEADER (RAM) ======
void writeWavHeader(uint8_t *buf, uint32_t dataSize) {
  uint32_t fileSize = 36 + dataSize;
  uint32_t byteRate = SAMPLE_RATE * 2;
  uint32_t sampleRate = SAMPLE_RATE;   // ✅ local variable

  memcpy(buf, "RIFF", 4);
  memcpy(buf + 4, &fileSize, 4);
  memcpy(buf + 8, "WAVEfmt ", 8);

  uint32_t subChunk1Size = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint16_t bitsPerSample = 16;
  uint16_t blockAlign = 2;

  memcpy(buf + 16, &subChunk1Size, 4);
  memcpy(buf + 20, &audioFormat, 2);
  memcpy(buf + 22, &numChannels, 2);
  memcpy(buf + 24, &sampleRate, 4);   // ✅ FIXED
  memcpy(buf + 28, &byteRate, 4);
  memcpy(buf + 32, &blockAlign, 2);
  memcpy(buf + 34, &bitsPerSample, 2);

  memcpy(buf + 36, "data", 4);
  memcpy(buf + 40, &dataSize, 4);
}


// ================= RECORD AUDIO ==========
void recordAudioToRAM() {
  wavSize = 0;
  uint32_t offset = 44;

  Serial.println("🎙 Recording...");
  digitalWrite(LED_PIN, HIGH);
  isRecording = true;

  unsigned long start = millis();
  while (millis() - start < RECORD_SECONDS * 1000) {
    size_t bytesRead;
    i2s_read(I2S_NUM_0, micBuffer, sizeof(micBuffer), &bytesRead, portMAX_DELAY);

    int samples = bytesRead / 4;
    for (int i = 0; i < samples; i++) {
      float s = (float)(micBuffer[i] >> 8) * MIC_GAIN;
      pcmBuffer[i] = constrain((int)s, -32768, 32767);
    }

    memcpy(wavBuffer + offset, pcmBuffer, samples * 2);
    offset += samples * 2;
    wavSize += samples * 2;
  }

  writeWavHeader(wavBuffer, wavSize);
  wavSize += 44;

  digitalWrite(LED_PIN, LOW);
  isRecording = false;
  Serial.println("✅ Recording done");
}

/// ================= SEND + STREAM PLAYBACK =================
void sendToSTTAndPlay() {
    if (!wavBuffer) return;

    WiFiClient client;
    if (!client.connect("192.168.137.1", 8000)) {
        Serial.println("❌ Connection failed");
        free(wavBuffer);
        wavBuffer = NULL;
        return;
    }
digitalWrite(LED_PLAYING, HIGH);

    Serial.println("📤 Sending audio (streaming)...");

    // Send HTTP POST headers
    client.printf(
        "POST /stt HTTP/1.1\r\n"
        "Host: 192.168.137.1\r\n"
        "Content-Type: audio/wav\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n\r\n",
        wavSize
    );

    // Stream WAV in chunks
    size_t offset = 0;
    while (offset < wavSize) {
        size_t chunk = (wavSize - offset > 1024) ? 1024 : (wavSize - offset);
        client.write(wavBuffer + offset, chunk);
        offset += chunk;
        delay(1); // give ESP32 some breathing room
    }

    free(wavBuffer);
    wavBuffer = NULL;

    Serial.println("▶️ Waiting for TTS stream...");
digitalWrite(LED_PLAYING, LOW);

digitalWrite(LED_WAITING, HIGH);

    uint8_t buffer[2048];
    int headerSkipped = 0;

    // Skip HTTP headers first
    while (client.connected() && headerSkipped == 0) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break; // end of headers
    }

    Serial.println("▶️ Playing TTS...");
    digitalWrite(LED_PLAYING, HIGH);
digitalWrite(LED_WAITING, LOW);


    // Stream audio as it arrives
    while (client.connected() || client.available()) {
        int len = client.available();
        if (len <= 0) {
            delay(10);
            continue;
        }

        int readBytes = client.readBytes(buffer, (len < sizeof(buffer)) ? len : sizeof(buffer));

        int offsetAudio = 0;
        // Skip WAV header (first 44 bytes)
        if (headerSkipped < 44) {
            int skip = 44 - headerSkipped;
            if (readBytes <= skip) {
                headerSkipped += readBytes;
                continue;
            }
            offsetAudio = skip;
            readBytes -= skip;
            headerSkipped = 44;
        }

        size_t written;
        i2s_write(I2S_NUM_1, buffer + offsetAudio, readBytes, &written, portMAX_DELAY);
    }

    Serial.println("⏹ Playback done");
    digitalWrite(LED_PLAYING, LOW);

    client.stop();
}


// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_IDLE, OUTPUT);
  pinMode(LED_WAITING, OUTPUT);
  pinMode(LED_PLAYING, OUTPUT);


  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);
  Serial.println(" WiFi connected");

  // -------- MIC I2S --------
  i2s_config_t mic_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512
  };

  i2s_pin_config_t mic_pins = {
    .bck_io_num = MIC_SCK,
    .ws_io_num  = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  i2s_driver_install(I2S_NUM_0, &mic_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &mic_pins);

  // -------- SPEAKER I2S --------
  i2s_config_t spk_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t spk_pins = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num  = SPK_LRC,
    .data_out_num = SPK_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_1, &spk_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &spk_pins);

  Serial.println("🚀 System ready");
}

// ================= LOOP ==================
void loop() {
  static unsigned long idleTimer = 0;
if (!isRecording) {
  if (millis() - idleTimer > 500) {
    digitalWrite(LED_IDLE, !digitalRead(LED_IDLE));
    idleTimer = millis();
  }
} else {
  digitalWrite(LED_IDLE, LOW);
}

  static bool lastBtn = HIGH;
  bool btn = digitalRead(BTN_PIN);

  if (lastBtn == HIGH && btn == LOW && !isRecording) {
    delay(30);
    if (digitalRead(BTN_PIN) == LOW) {
      wavBuffer = (uint8_t *)ps_malloc(MAX_AUDIO_BYTES);
      if (!wavBuffer) {
        Serial.println(" PSRAM alloc failed");
        return;
      }
      recordAudioToRAM();
      sendToSTTAndPlay();
    }
  }
  lastBtn = btn;
}
