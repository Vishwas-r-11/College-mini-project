#include <Arduino.h>
#include <driver/i2s.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WitAITTS.h>

// ============================================================================
//                              WIFI CONFIGURATION
// ============================================================================
const char* ssid     = "JioFiber-PUl8o";
const char* password = "ramesh-Gayathri,251114";

// ============================================================================
//                             GEMINI AI CONFIGURATION
// ============================================================================
const char* geminiApiKey = "YOUR_GEMINI_API_KEY"; // <-- Enter your Google Gemini API Key here
const char* geminiModel  = "gemini-1.5-flash";      // e.g. "gemini-1.5-flash" or "gemini-2.0-flash"
const int   maxTokens    = 300;

// ============================================================================
//                             WIT.AI CONFIGURATION
// ============================================================================
// Wit.ai Server Access Token (used for both STT and TTS)
const char* witToken  = "Z62K6NE3EDFQNBCYKNQNNWL3JGBK3L54";
const char* witSttUrl = "https://api.wit.ai/speech?v=20220622";

// ============================================================================
//                         HARDWARE PIN CONFIGURATION
// ============================================================================
// INMP441 Microphone (I2S Input - RX)
#define MIC_I2S_WS   7   // LRC (Word Select)  - GPIO07
#define MIC_I2S_SD   8   // SD  (Serial Data)  - GPIO08
#define MIC_I2S_SCK  10  // SCK (Serial Clock) - GPIO10

// MAX98357A Speaker Amplifier (I2S Output - TX)
#define SPK_I2S_BCLK 6   // BCLK (Bit Clock)    - GPIO06
#define SPK_I2S_LRC  11   // LRC  (Word Select)  - GPIO11
#define SPK_I2S_DIN  12  // DIN  (Data In)      - GPIO12

// ============================================================================
//                         AUDIO / I2S CONFIGURATION
// ============================================================================
#define MIC_I2S_PORT         I2S_NUM_1
#define SAMPLE_RATE          16000
#define BITS_PER_SAMPLE      16
#define I2S_BUFFER_SIZE      512
#define RECORD_TIME_SECONDS  5   // Max recording duration in seconds
#define VOLUME_GAIN          4    // Digital mic sensitivity multiplier

// Audio buffer for STT recording
const int maxRecordSize = SAMPLE_RATE * RECORD_TIME_SECONDS * 2; // 2 bytes per sample (16-bit)
uint8_t* audioBuffer = nullptr;
int audioBufferIndex = 0;
bool isRecording = false;
bool micDriverInstalled = false;
unsigned long lastDisplayTime = 0;

// Forward declaration of pipeline function
void processVoicePipeline();

// WitAITTS instance for speaker playback
WitAITTS tts(SPK_I2S_BCLK, SPK_I2S_LRC, SPK_I2S_DIN);

// ============================================================================
//                     MICROPHONE (I2S RX) FUNCTIONS
// ============================================================================

// Initialize I2S driver for INMP441
void i2sInitMic() {
  if (micDriverInstalled) return;

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
    .bck_io_num = MIC_I2S_SCK,
    .ws_io_num = MIC_I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_I2S_SD
  };

  esp_err_t err = i2s_driver_install(MIC_I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("❌ Failed to install Mic I2S driver: %d\n", err);
    return;
  }

  err = i2s_set_pin(MIC_I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("❌ Failed to set Mic I2S pins: %d\n", err);
    return;
  }

  micDriverInstalled = true;
  Serial.println("🎤 Mic I2S initialized.");
}

// De-initialize I2S driver to release bus cleanly
void i2sDeinitMic() {
  if (micDriverInstalled) {
    i2s_driver_uninstall(MIC_I2S_PORT);
    micDriverInstalled = false;
  }
}

// Amplify audio samples digitally to prevent quiet speech truncation
void applyGain(int16_t* samples, int count) {
  for (int i = 0; i < count; i++) {
    int32_t val = (int32_t)samples[i] * VOLUME_GAIN;
    if (val > 32767) val = 32767;
    else if (val < -32768) val = -32768;
    samples[i] = (int16_t)val;
  }
}

// Start voice recording
void startRecording() {
  if (tts.isPlaying()) {
    tts.stop();
  }

  i2sInitMic();
  audioBufferIndex = 0;
  isRecording = true;
  lastDisplayTime = millis();
  Serial.println("\n🎤 Recording started... Speak now! (Send '2' to stop)");
}

// Stop voice recording
void stopRecording() {
  isRecording = false;
  i2sDeinitMic();
  Serial.println("\n⏹️  Recording stopped.");
  Serial.printf("Recorded %d bytes (%.2f seconds)\n",
                audioBufferIndex,
                (float)audioBufferIndex / 2.0 / SAMPLE_RATE);
}

// Capture incoming audio from I2S DMA
void recordAudio() {
  if (!isRecording) return;

  size_t bytesRead = 0;
  uint8_t i2sData[I2S_BUFFER_SIZE];

  esp_err_t result = i2s_read(MIC_I2S_PORT, &i2sData, I2S_BUFFER_SIZE, &bytesRead, portMAX_DELAY);

  if (result == ESP_OK && bytesRead > 0) {
    int sampleCount = bytesRead / sizeof(int16_t);
    applyGain((int16_t*)i2sData, sampleCount);

    if (audioBufferIndex + bytesRead < maxRecordSize) {
      memcpy(audioBuffer + audioBufferIndex, i2sData, bytesRead);
      audioBufferIndex += bytesRead;
    } else {
      Serial.println("\n⚠️  Maximum recording time reached!");
      stopRecording();
      processVoicePipeline();
    }
  }

  // Live recording status indicator
  if (millis() - lastDisplayTime >= 800) {
    lastDisplayTime = millis();
    Serial.printf("...recording (%.1fs)...\n", (float)audioBufferIndex / 2.0 / SAMPLE_RATE);
  }
}

// ============================================================================
//                   SPEECH-TO-TEXT (WIT.AI STT)
// ============================================================================

// Extract full sentence from Wit.ai multi-chunk/NDJSON stream
String parseWitAiResponse(const String& response) {
  String bestText = "";
  int searchPos = 0;

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

// Send raw PCM audio to Wit.ai STT endpoint
String sendAudioToWitAi() {
  if (audioBufferIndex == 0) {
    Serial.println("❌ No audio data recorded.");
    return "";
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected!");
    return "";
  }

  Serial.println("\n📤 Sending audio to Wit.ai STT...");

  HTTPClient http;
  http.setTimeout(15000); // 15 seconds timeout
  http.begin(witSttUrl);
  http.addHeader("Authorization", String("Bearer ") + witToken);
  http.addHeader("Content-Type", "audio/raw;encoding=signed-integer;bits=16;rate=16000;endian=little");

  int httpResponseCode = http.POST(audioBuffer, audioBufferIndex);
  String transcribedText = "";

  if (httpResponseCode > 0) {
    String response = http.getString();
    transcribedText = parseWitAiResponse(response);

    if (transcribedText.length() > 0) {
      Serial.println("\n✅ Transcribed Speech:");
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      Serial.println(transcribedText);
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    } else {
      Serial.println("⚠️ Wit.ai returned no recognizable speech.");
    }
  } else {
    Serial.printf("❌ HTTP STT Error: %d - %s\n", httpResponseCode, http.errorToString(httpResponseCode).c_str());
  }

  http.end();
  return transcribedText;
}

// ============================================================================
//                           GEMINI AI (LLM)
// ============================================================================

String askGemini(const String& question) {
  if (question.length() == 0) return "";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60000);

  HTTPClient https;
  String url = "https://generativelanguage.googleapis.com/v1beta/models/" +
               String(geminiModel) +
               ":generateContent?key=" +
               String(geminiApiKey);

  if (!https.begin(client, url)) {
    Serial.println("❌ Could not connect to Gemini API.");
    return "";
  }

  https.setTimeout(60000);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("User-Agent", "ESP32-S3-Voice-Assistant");

  // Create JSON Request for Gemini
  JsonDocument requestDoc;
  JsonArray contents = requestDoc["contents"].to<JsonArray>();
  JsonObject content = contents.add<JsonObject>();
  JsonArray parts = content["parts"].to<JsonArray>();
  JsonObject part = parts.add<JsonObject>();

  part["text"] = question +
                 "\n\nAnswer briefly and naturally. "
                 "Your answer will be converted to speech, "
                 "so do not use markdown, tables, emojis, bullet points, "
                 "or unnecessary formatting. Keep the answer "
                 "under 250 characters if possible.";

  JsonObject generationConfig = requestDoc["generationConfig"].to<JsonObject>();
  generationConfig["maxOutputTokens"] = maxTokens;

  String requestBody;
  serializeJson(requestDoc, requestBody);

  Serial.println("\n🧠 Asking Gemini...");
  int httpCode = https.POST(requestBody);

  if (httpCode == HTTP_CODE_OK) {
    String payload = https.getString();
    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, payload);

    if (error) {
      Serial.print("❌ Gemini JSON parsing error: ");
      Serial.println(error.c_str());
      https.end();
      return "";
    }

    const char* rawAnswer = responseDoc["candidates"][0]["content"]["parts"][0]["text"];
    if (rawAnswer != nullptr) {
      String answer = String(rawAnswer);
      answer.trim();

      Serial.println("\n💡 GEMINI ANSWER:");
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      Serial.println(answer);
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

      https.end();
      return answer;
    }
  } else {
    Serial.printf("❌ Gemini HTTP Error: %d\n", httpCode);
    String errResponse = https.getString();
    if (errResponse.length() > 0) {
      Serial.println(errResponse);
    }
  }

  https.end();
  return "";
}

// ============================================================================
//                   TEXT-TO-SPEECH (WIT.AI TTS / SPEAKER)
// ============================================================================

void speakAnswer(String answer) {
  answer.trim();
  if (answer.length() == 0) return;

  // WitAITTS maximum = 280 characters limit
  if (answer.length() > 280) {
    Serial.println("⚠️ Answer is longer than 280 characters. Truncating for TTS...");
    answer = answer.substring(0, 280);
    int lastSpace = answer.lastIndexOf(' ');
    if (lastSpace > 200) {
      answer = answer.substring(0, lastSpace);
    }
    answer += ".";
  }

  Serial.println("\n🔊 Speaking response via Wit.ai TTS...");
  bool result = tts.speak(answer);

  if (result) {
    Serial.println("✓ Audio playback started.");
  } else {
    Serial.println("❌ Wit.ai TTS playback request failed.");
  }
}

// ============================================================================
//                      FULL VOICE AI PIPELINE
// ============================================================================

void processVoicePipeline() {
  // Step 1: Transcribe Speech to Text via Wit.ai STT
  String userQuestion = sendAudioToWitAi();

  if (userQuestion.length() == 0) {
    Serial.println("\nReady for next command. Send '1' to record.\n");
    return;
  }

  // Step 2: Query Google Gemini AI
  String aiAnswer = askGemini(userQuestion);

  // Step 3: Speak Gemini AI answer via MAX98357A speaker
  if (aiAnswer.length() > 0) {
    speakAnswer(aiAnswer);
  }

  Serial.println("\nReady for next interaction. Send '1' to record.\n");
}

// ============================================================================
//                                SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔═══════════════════════════════════════════════════════════╗");
  Serial.println("║       ESP32-S3 FULL VOICE AI ASSISTANT                    ║");
  Serial.println("║   INMP441 (STT) + Google Gemini + MAX98357A (TTS)        ║");
  Serial.println("╚═══════════════════════════════════════════════════════════╝");

  // Allocate recording buffer safely depending on PSRAM availability
  #if defined(BOARD_HAS_PSRAM)
    audioBuffer = (uint8_t*)ps_malloc(maxRecordSize);
  #else
    audioBuffer = (uint8_t*)malloc(maxRecordSize);
  #endif

  if (audioBuffer == nullptr) {
    Serial.println("❌ Failed to allocate memory for audio buffer!");
    while (1) delay(1000);
  }
  Serial.printf("✓ Audio recording buffer allocated: %d bytes (%.1fs)\n", maxRecordSize, (float)RECORD_TIME_SECONDS);

  // Initialize Wi-Fi stack first
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("\nConnecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✓ Wi-Fi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Disable Wi-Fi sleep ONLY after connection is fully active
  WiFi.setSleep(WIFI_PS_NONE);

  // Initialize Wit.ai TTS
  Serial.println("\nInitializing Wit.ai TTS...");
  tts.setDebugLevel(DEBUG_INFO);
  if (tts.begin(ssid, password, witToken)) {
    Serial.println("✓ Wit.ai TTS initialized!");
    tts.setVoice("wit$Colin");
    tts.setStyle("default");
    tts.setSpeed(100);
    tts.setPitch(100);
    tts.setGain(0.5);
    tts.printConfig();
  } else {
    Serial.println("❌ Wit.ai TTS initialization failed! Check token.");
  }

  Serial.println("\n============================================================");
  Serial.println("                       SYSTEM READY                         ");
  Serial.println("============================================================");
  Serial.println("📝 Usage Options:");
  Serial.println("   [VOICE MODE]");
  Serial.println("   1. Type '1' and press Send  -> Start recording speech");
  Serial.println("   2. Type '2' and press Send  -> Stop & send to Gemini -> Speak");
  Serial.println("   [TEXT MODE]");
  Serial.println("   - Type any question directly and press Enter");
  Serial.println("============================================================\n");
}

// ============================================================================
//                                 LOOP
// ============================================================================

void loop() {
  // Service background audio streaming for TTS playback
  tts.loop();

  // Check for incoming serial commands
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() > 0) {
      if (input == "1") {
        if (!isRecording) {
          startRecording();
        } else {
          Serial.println("\n⚠️ Already recording! Send '2' to stop.");
        }
      } else if (input == "2") {
        if (isRecording) {
          stopRecording();
          processVoicePipeline();
        } else {
          Serial.println("\n⚠️ Not currently recording! Send '1' to start.");
        }
      } else {
        // Direct text question input fallback
        if (isRecording) {
          stopRecording();
        }
        Serial.println("\n⌨️ Text Question received: " + input);
        String aiAnswer = askGemini(input);
        if (aiAnswer.length() > 0) {
          speakAnswer(aiAnswer);
        }
      }
    }
  }

  // Record audio if recording is active
  if (isRecording) {
    recordAudio();
  }

  delay(1);
}
