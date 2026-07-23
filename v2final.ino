#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>

// ================= WIFI =================
#define WIFI_SSID "SALLU"
#define WIFI_PASS "123456789"

// ================= SERVER ===============
#define STT_SERVER_URL "http://192.168.137.1:8000/stt"

// ================= MIC (INMP441) =========
#define MIC_WS   41
#define MIC_SCK  40
#define MIC_SD   42

// ================= SPEAKER (MAX98357A) =====
#define SPK_LRC   5
#define SPK_BCLK  7
#define SPK_DIN   18

// ================= SD ===================
#define SD_CS    10
#define SPI_MOSI 11
#define SPI_MISO 13
#define SPI_SCK  12

// ================= BUTTON / LED =========
#define BTN_PIN  4
#define LED_PIN  21

// ================= AUDIO =================
#define SAMPLE_RATE     16000
#define BUFFER_LEN      512
#define RECORD_SECONDS  3
#define MIC_GAIN        0.3f   // IMPORTANT

int32_t micBuffer[BUFFER_LEN];
int16_t pcmBuffer[BUFFER_LEN];

// ================= WAV ===================
File wavFile;
uint32_t dataSize = 0;
String lastFilename;

// ================= STATE =================
bool isRecording = false;
unsigned long lastBlink = 0;
bool ledState = false;

// ================= DEBUG =================
void debug(const String &msg) {
  Serial.print("[DEBUG] ");
  Serial.println(msg);
}

// ================= WAV HEADER =============
void writeWavHeader(File &file) {
  file.seek(0);
  uint32_t sampleRate = SAMPLE_RATE;
  uint32_t fileSize = 36 + dataSize;
  uint32_t subChunk1Size = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint16_t bitsPerSample = 16;
  uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
  uint16_t blockAlign = numChannels * bitsPerSample / 8;

  file.write((const uint8_t *)"RIFF", 4);
  file.write((uint8_t *)&fileSize, 4);
  file.write((const uint8_t *)"WAVE", 4);
  file.write((const uint8_t *)"fmt ", 4);

  file.write((uint8_t *)&subChunk1Size, 4);
  file.write((uint8_t *)&audioFormat, 2);
  file.write((uint8_t *)&numChannels, 2);
  file.write((uint8_t *)&sampleRate, 4);
  file.write((uint8_t *)&byteRate, 4);
  file.write((uint8_t *)&blockAlign, 2);
  file.write((uint8_t *)&bitsPerSample, 2);

  file.write((const uint8_t *)"data", 4);
  file.write((uint8_t *)&dataSize, 4);
}

// ================= PLAY WAV FROM SD ==========
void playWavFromSD(const char *filename) {
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    Serial.println("❌ Failed to open WAV");
    return;
  }

  file.seek(44); // skip WAV header
  uint8_t buffer[512];
  size_t bytesRead, bytesWritten;

  Serial.println("▶️ Playing audio...");
  while ((bytesRead = file.read(buffer, sizeof(buffer))) > 0) {
    i2s_write(I2S_NUM_1, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }

  file.close();
  Serial.println("⏹ Playback finished");
}

// ================= SEND TO STT =============
// ================= SEND TO STT AND PLAY DIRECTLY =================
void sendToSTT() {
  debug("Uploading WAV to STT server...");

  File file = SD.open(lastFilename, FILE_READ);
  if (!file) {
    debug("❌ Failed to open WAV");
    return;
  }

  size_t fileSize = file.size();
  uint8_t *wavData = (uint8_t *)malloc(fileSize);
  if (!wavData) {
    debug("❌ malloc failed");
    file.close();
    return;
  }

  file.read(wavData, fileSize);
  file.close();

  HTTPClient http;
  http.begin(STT_SERVER_URL);
  http.addHeader("Content-Type", "audio/wav");
  http.setTimeout(120000);

  int httpCode = http.POST(wavData, fileSize);
  free(wavData);

  if (httpCode != HTTP_CODE_OK) {
    debug("❌ HTTP error");
    http.end();
    return;
  }

  // ------------------ STREAM WAV DIRECTLY ------------------
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[2048];       // 1 KB buffer
  int len;

  Serial.println("▶️ Playing TTS...");

  // Skip WAV header (44 bytes)
  int headerSkipped = 0;

  while (http.connected() && (len = stream->read(buffer, sizeof(buffer))) > 0) {
    int offset = 0;

    // Skip WAV header only for first chunk
    if (headerSkipped < 44) {
      int skip = 44 - headerSkipped;
      if (len <= skip) {
        headerSkipped += len;
        continue; // skip entire chunk
      }
      offset = skip;
      len -= skip;
      headerSkipped = 44;
    }

    size_t bytesWritten;
    i2s_write(I2S_NUM_1, buffer + offset, len, &bytesWritten, portMAX_DELAY);
  }

  Serial.println("⏹ TTS Playback finished");
  http.end();

  SD.remove(lastFilename);
  debug("✅ WAV deleted");
}


// ================= RECORD AUDIO ============
void recordAudio() {
  lastFilename = "/rec_" + String(millis()) + ".wav";
  debug("Creating file: " + lastFilename);

  wavFile = SD.open(lastFilename, FILE_WRITE);
  if (!wavFile) {
    debug("File open failed");
    return;
  }

  isRecording = true;
  digitalWrite(LED_PIN, HIGH);

  dataSize = 0;
  writeWavHeader(wavFile);

  debug("🎙 Recording started");
  unsigned long start = millis();

  while (millis() - start < RECORD_SECONDS * 1000) {
    size_t bytesRead;
    i2s_read(I2S_NUM_0, micBuffer, sizeof(micBuffer), &bytesRead, portMAX_DELAY);

    int samples = bytesRead / 4;
    long long sumSq = 0;

    for (int i = 0; i < samples; i++) {
      float s = (float)(micBuffer[i] >> 8);
      s *= MIC_GAIN;
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      pcmBuffer[i] = (int16_t)s;
      sumSq += (long long)(s * s);
    }

    int rms = sqrt((double)sumSq / samples);
    Serial.println(rms);

    wavFile.write((uint8_t *)pcmBuffer, samples * 2);
    dataSize += samples * 2;
  }

  writeWavHeader(wavFile);
  wavFile.close();

  isRecording = false;
  debug("✅ Recording finished");

  sendToSTT();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  debug("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  debug("WiFi connected");

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    debug("❌ SD mount failed");
    while (1);
  }
  debug("SD mounted");

  // ---------- MIC I2S -----------
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_LEN
  };

  i2s_pin_config_t pins = {
    .bck_io_num = MIC_SCK,
    .ws_io_num  = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_start(I2S_NUM_0);

  // ---------- SPEAKER I2S -----------
  i2s_config_t spk_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t spk_pins = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num  = SPK_LRC,
    .data_out_num = SPK_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_1, &spk_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &spk_pins);

  debug("System ready");
}

// ================= LOOP ==================
void loop() {
  static bool lastBtn = HIGH;
  bool btn = digitalRead(BTN_PIN);

  // Blink LED when idle
  if (!isRecording && millis() - lastBlink > 500) {
    lastBlink = millis();
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }

  // Button pressed → record
  if (lastBtn == HIGH && btn == LOW && !isRecording) {
    delay(30);
    if (digitalRead(BTN_PIN) == LOW) {
      recordAudio();
    }
  }
  lastBtn = btn;
}
