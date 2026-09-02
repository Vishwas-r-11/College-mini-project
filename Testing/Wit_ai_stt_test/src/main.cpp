#include <Arduino.h>
#include <driver/i2s.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "JioFiber-PUl8o";
const char* password = "ramesh-Gayathri,251114";

// Wit.ai credentials
const char* witAiToken = "Z62K6NE3EDFQNBCYKNQNNWL3JGBK3L54";
const char* witAiUrl = "https://api.wit.ai/speech?v=20220622";

// I2S pins for INMP441
#define I2S_WS 7    // LRC (Word Select) - GPIO07
#define I2S_SD 8    // SD (Serial Data) - GPIO08
#define I2S_SCK 10  // SCK (Serial Clock) - GPIO10


// I2S configuration
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16
#define I2S_BUFFER_SIZE 512
#define RECORD_TIME_SECONDS 10  // Maximum recording time

// Audio buffer
const int maxRecordSize = SAMPLE_RATE * RECORD_TIME_SECONDS * 2; // 2 bytes per sample
uint8_t* audioBuffer = nullptr;
int audioBufferIndex = 0;
bool isRecording = false;

// Function to initialize I2S
void i2sInit() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = I2S_BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed to install I2S driver: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S pins: %d\n", err);
    return;
  }

  Serial.println("I2S initialized successfully");
}

// Function to connect to WiFi
void connectWiFi() {
  Serial.println("\nConnecting to WiFi...");
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

// Function to start recording
void startRecording() {
  audioBufferIndex = 0;
  isRecording = true;
  Serial.println("\n🎤 Recording started... Press Enter to stop.");
}

// Function to stop recording and process
void stopRecording() {
  isRecording = false;
  Serial.println("⏹️  Recording stopped.");
  Serial.printf("Recorded %d bytes (%d samples, %.2f seconds)\n",
                audioBufferIndex,
                audioBufferIndex / 2,
                (float)audioBufferIndex / 2.0 / SAMPLE_RATE);
}

// Function to send audio to Wit.ai for speech-to-text
void sendToWitAi() {
  if (audioBufferIndex == 0) {
    Serial.println("No audio data to send!");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }

  Serial.println("\n📤 Sending audio to Wit.ai...");

  HTTPClient http;
  http.begin(witAiUrl);
  http.addHeader("Authorization", String("Bearer ") + witAiToken);
  http.addHeader("Content-Type", "audio/raw;encoding=signed-integer;bits=16;rate=16000;endian=little");

  int httpResponseCode = http.POST(audioBuffer, audioBufferIndex);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("Response code: %d\n", httpResponseCode);

    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      Serial.println("Raw response:");
      Serial.println(response);
    } else {
      // Extract text from response
      if (doc.containsKey("text")) {
        String transcribedText = doc["text"].as<String>();
        Serial.println("\n✅ Transcribed Text:");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.println(transcribedText);
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      } else {
        Serial.println("No text found in response");
        Serial.println("Full response:");
        Serial.println(response);
      }
    }
  } else {
    Serial.printf("Error on HTTP request: %d\n", httpResponseCode);
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}

// Function to record audio samples
void recordAudio() {
  if (!isRecording) return;

  size_t bytesRead = 0;
  uint8_t i2sData[I2S_BUFFER_SIZE];

  esp_err_t result = i2s_read(I2S_PORT, &i2sData, I2S_BUFFER_SIZE, &bytesRead, portMAX_DELAY);

  if (result == ESP_OK && bytesRead > 0) {
    // Check if buffer has space
    if (audioBufferIndex + bytesRead < maxRecordSize) {
      memcpy(audioBuffer + audioBufferIndex, i2sData, bytesRead);
      audioBufferIndex += bytesRead;
    } else {
      // Buffer full, stop recording
      Serial.println("\n⚠️  Maximum recording time reached!");
      stopRecording();
      sendToWitAi();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n╔═══════════════════════════════════════════════════════╗");
  Serial.println("║   INMP441 I2S Microphone - Speech to Text (Wit.ai)   ║");
  Serial.println("╚═══════════════════════════════════════════════════════╝");

  // Allocate audio buffer
  audioBuffer = (uint8_t*)malloc(maxRecordSize);
  if (audioBuffer == nullptr) {
    Serial.println("Failed to allocate memory for audio buffer!");
    while(1) delay(1000);
  }
  Serial.printf("Audio buffer allocated: %d bytes\n", maxRecordSize);

  // Initialize I2S
  i2sInit();

  // Connect to WiFi
  connectWiFi();

  Serial.println("\n📝 Instructions:");
  Serial.println("   1. Press Enter to START recording");
  Serial.println("   2. Speak into the microphone");
  Serial.println("   3. Press Enter a to STOP and transcribe");
  Serial.println("\nReady! Waiting for your command...\n");
}

void loop() {
  // Check for serial input
  if (Serial.available() > 0) {
    char input = Serial.read();

    // Clear the serial buffer
    while (Serial.available() > 0) {
      Serial.read();
    }

    if (input == '\n' || input == '\r') {
      if (!isRecording) {
        // Start recording
        startRecording();
      } else {
        // Stop recording and send to Wit.ai
        stopRecording();
        sendToWitAi();
        Serial.println("\nReady for next recording. Press Enter to start...\n");
      }
    }
  }

  // Record audio if recording is active
  if (isRecording) {
    recordAudio();
  }

  delay(1);
}