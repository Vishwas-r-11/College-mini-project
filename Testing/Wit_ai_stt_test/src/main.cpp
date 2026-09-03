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

// I2S pins for INMP441 (ESP32-S3 configured pins)
#define I2S_WS 7    // LRC (Word Select) - GPIO07
#define I2S_SD 8    // SD (Serial Data) - GPIO08
#define I2S_SCK 10  // SCK (Serial Clock) - GPIO10

// Audio configuration
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16
#define I2S_BUFFER_SIZE 512
#define RECORD_TIME_SECONDS 10  // Maximum recording time
#define VOLUME_GAIN 4           // Software gain multiplier (boosts mic sensitivity)

// Audio buffer
const int maxRecordSize = SAMPLE_RATE * RECORD_TIME_SECONDS * 2; // 2 bytes per sample
uint8_t* audioBuffer = nullptr;
int audioBufferIndex = 0;
bool isRecording = false;
unsigned long lastDisplayTime = 0;

// Function to amplify audio samples
void applyGain(int16_t* samples, int count) {
  for (int i = 0; i < count; i++) {
    int32_t val = (int32_t)samples[i] * VOLUME_GAIN;
    // Clip to 16-bit signed range [-32768, 32767]
    if (val > 32767) val = 32767;
    else if (val < -32768) val = -32768;
    samples[i] = (int16_t)val;
  }
}

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
  lastDisplayTime = millis();
  Serial.println("\n🎤 Recording started... Speak now! (Send '2' to stop)");
}

// Function to stop recording and process
void stopRecording() {
  isRecording = false;
  Serial.println("\n⏹️  Recording stopped.");
  Serial.printf("Recorded %d bytes (%d samples, %.2f seconds)\n",
                audioBufferIndex,
                audioBufferIndex / 2,
                (float)audioBufferIndex / 2.0 / SAMPLE_RATE);
}

// Function to extract full text from Wit.ai multi-chunk/NDJSON response
String parseWitAiResponse(const String& response) {
  String bestText = "";
  int searchPos = 0;

  // Wit.ai returns chunked/newline-delimited JSON (NDJSON)
  // We iterate through every JSON object in the response to extract the final full text
  while (searchPos < response.length()) {
    int objStart = response.indexOf('{', searchPos);
    if (objStart == -1) break;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response.substring(objStart));
    if (!error) {
      if (!doc["text"].isNull()) {
        String currentText = doc["text"].as<String>();
        if (currentText.length() > 0) {
          bestText = currentText;
        }
      }
      // If final flag is present, this is definitely the complete sentence
      if (doc["is_final"].is<bool>() && doc["is_final"].as<bool>()) {
        break;
      }
    }

    int nextLine = response.indexOf('\n', objStart);
    if (nextLine != -1) {
      searchPos = nextLine + 1;
    } else {
      break;
    }
  }

  return bestText;
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
  http.setTimeout(15000); // 15 seconds timeout
  http.begin(witAiUrl);
  http.addHeader("Authorization", String("Bearer ") + witAiToken);
  http.addHeader("Content-Type", "audio/raw;encoding=signed-integer;bits=16;rate=16000;endian=little");

  int httpResponseCode = http.POST(audioBuffer, audioBufferIndex);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("Response code: %d\n", httpResponseCode);

    String transcribedText = parseWitAiResponse(response);

    if (transcribedText.length() > 0) {
      Serial.println("\n✅ Transcribed Text:");
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      Serial.println(transcribedText);
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    } else {
      Serial.println("No text found in response.");
      Serial.println("Raw response:");
      Serial.println(response);
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
    // Apply digital volume gain to boost microphone sensitivity
    int sampleCount = bytesRead / sizeof(int16_t);
    applyGain((int16_t*)i2sData, sampleCount);

    // Check if buffer has space
    if (audioBufferIndex + bytesRead < maxRecordSize) {
      memcpy(audioBuffer + audioBufferIndex, i2sData, bytesRead);
      audioBufferIndex += bytesRead;
    } else {
      // Buffer full, stop recording
      Serial.println("\n⚠️  Maximum recording time reached!");
      stopRecording();
      sendToWitAi();
      Serial.println("\nReady. Send '1' to start recording.\n");
    }
  }

  // Display 'recording' periodically while active
  if (millis() - lastDisplayTime >= 800) {
    lastDisplayTime = millis();
    Serial.printf("...recording (%.1fs)...\n", (float)audioBufferIndex / 2.0 / SAMPLE_RATE);
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
  Serial.println("   1. Type '1' and press Send to START recording");
  Serial.println("   2. Speak into the microphone");
  Serial.println("   3. Type '2' and press Send to STOP and transcribe");
  Serial.println("\nReady! Waiting for command (1 = Start, 2 = Stop)...\n");
}

void loop() {
  // Check for serial input
  if (Serial.available() > 0) {
    char input = Serial.read();

    if (input == '1') {
      if (!isRecording) {
        startRecording();
      } else {
        Serial.println("\n⚠️ Already recording! Send '2' to stop.");
      }
    } else if (input == '2') {
      if (isRecording) {
        stopRecording();
        sendToWitAi();
        Serial.println("\nReady for next recording. Send '1' to start...\n");
      } else {
        Serial.println("\n⚠️ Not currently recording! Send '1' to start.");
      }
    }
  }

  // Record audio if recording is active
  if (isRecording) {
    recordAudio();
  }

  delay(1);
}
