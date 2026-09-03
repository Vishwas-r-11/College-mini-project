#include <Arduino.h>
#include <vector>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s_std.h"
#include "Audio.h"

// =====================================================
//                    WIFI
// =====================================================

const char* ssid     = "WiFi name";
const char* password = "Wifi password";

// =====================================================
//                    API KEYS
// =====================================================

const char* witAiToken = "YOUR_WIT_AI_TOKEN"; // Replace with your Wit.ai token

const char* witAiUrl = "https://api.wit.ai/speech?v=20220622";

const char* geminiApiKey ="YOUR_GEMINI_API_KEY"; // Replace with your Gemini API key

// Use a model currently available to your API key.
// Change this if necessary.
const char* geminiModel = "gemini-3.5-flash-lite";

// =====================================================
//              INMP441 MICROPHONE
// =====================================================

#define MIC_I2S_PORT I2S_NUM_1

#define I2S_MIC_WS   7
#define I2S_MIC_SD   8
#define I2S_MIC_SCK  10

#define SAMPLE_RATE 16000
#define I2S_BUFFER_SIZE 512
#define VOLUME_GAIN 4           // Software gain multiplier (boosts mic sensitivity)

// Maximum recording time
#define RECORD_TIME_SECONDS 10

// 16-bit = 2 bytes/sample
const int maxRecordSize =
    SAMPLE_RATE * RECORD_TIME_SECONDS * 2;

// Audio buffer
uint8_t* audioBuffer = nullptr;

int audioBufferIndex = 0;

bool isRecording = false;

static i2s_chan_handle_t mic_rx_handle = NULL;

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

// =====================================================
//              MAX98357A SPEAKER
// =====================================================

#define I2S_SPK_BCLK 6
#define I2S_SPK_LRC  11
#define I2S_SPK_DOUT 12

Audio audio;


// =====================================================
//                    I2S MICROPHONE
// =====================================================

void initMicrophoneI2S()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = I2S_BUFFER_SIZE / 2;
    chan_cfg.auto_clear = false;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &mic_rx_handle);
    if (err != ESP_OK)
    {
        Serial.printf("❌ I2S channel create failed: %d\n", err);
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_MIC_SCK,
            .ws = (gpio_num_t)I2S_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)I2S_MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    err = i2s_channel_init_std_mode(mic_rx_handle, &std_cfg);
    if (err != ESP_OK)
    {
        Serial.printf("❌ I2S std init failed: %d\n", err);
        return;
    }

    err = i2s_channel_enable(mic_rx_handle);
    if (err != ESP_OK)
    {
        Serial.printf("❌ I2S channel enable failed: %d\n", err);
        return;
    }

    Serial.println("✓ INMP441 microphone initialized (I2S_NUM_1)");
}


// =====================================================
//                    WIFI
// =====================================================

void connectWiFi()
{
    Serial.println();
    Serial.println(
        "Connecting to WiFi..."
    );

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        ssid,
        password
    );

    int attempts = 0;

    while (
        WiFi.status() != WL_CONNECTED &&
        attempts < 30
    )
    {
        delay(500);

        Serial.print(".");

        attempts++;
    }


    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println();

        Serial.println(
            "✓ WiFi connected"
        );

        Serial.print(
            "IP address: "
        );

        Serial.println(
            WiFi.localIP()
        );
    }
    else
    {
        Serial.println();

        Serial.println(
            "❌ WiFi connection failed"
        );
    }
}


// =====================================================
//                 START RECORDING
// =====================================================

void startRecording()
{
    audioBufferIndex = 0;
    isRecording = true;

    Serial.println();
    Serial.println("🎤 RECORDING STARTED...");
    Serial.println("Speak now.");
    Serial.println("Send '2' and press ENTER to stop recording.");
}


// =====================================================
//                  STOP RECORDING
// =====================================================

void stopRecording()
{
    isRecording = false;

    Serial.println();
    Serial.println("⏹ Recording stopped.");

    float seconds = (float)audioBufferIndex / 2.0 / SAMPLE_RATE;
    Serial.printf("Recorded: %d bytes\n", audioBufferIndex);
    Serial.printf("Duration: %.2f seconds\n", seconds);
}


// =====================================================
//              RECORD MICROPHONE AUDIO
// =====================================================

void recordAudio()
{
    if (!isRecording || mic_rx_handle == NULL)
        return;

    uint8_t i2sData[I2S_BUFFER_SIZE];
    size_t bytesRead = 0;

    esp_err_t result = i2s_channel_read(
        mic_rx_handle,
        i2sData,
        I2S_BUFFER_SIZE,
        &bytesRead,
        pdMS_TO_TICKS(100)
    );

    if (result == ESP_OK && bytesRead > 0)
    {
        // Apply digital volume gain to boost microphone sensitivity
        int sampleCount = bytesRead / sizeof(int16_t);
        applyGain((int16_t*)i2sData, sampleCount);

        if (audioBufferIndex + bytesRead < maxRecordSize)
        {
            memcpy(audioBuffer + audioBufferIndex, i2sData, bytesRead);
            audioBufferIndex += bytesRead;
        }
        else
        {
            Serial.println();
            Serial.println("⚠ Maximum recording time reached.");
            stopRecording();
        }
    }
}


// =====================================================
//                  WIT.AI STT
// =====================================================

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

String sendToWitAI()
{
    if (audioBufferIndex <= 0)
    {
        Serial.println("❌ No audio recorded.");
        return "";
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("❌ WiFi disconnected.");
        return "";
    }

    Serial.println("\n📤 Sending audio to Wit.ai...");

    HTTPClient http;
    http.setTimeout(15000); // 15 seconds timeout
    http.begin(witAiUrl);
    http.addHeader("Authorization", String("Bearer ") + witAiToken);
    http.addHeader("Content-Type", "audio/raw;encoding=signed-integer;bits=16;rate=16000;endian=little");

    int httpResponseCode = http.POST(audioBuffer, audioBufferIndex);

    if (httpResponseCode > 0)
    {
        String response = http.getString();
        Serial.printf("Wit.ai response code: %d\n", httpResponseCode);

        String transcribedText = parseWitAiResponse(response);

        if (transcribedText.length() > 0)
        {
            Serial.println("\n✅ Transcribed Text:");
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            Serial.println(transcribedText);
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            http.end();
            return transcribedText;
        }
        else
        {
            Serial.println("No text found in response.");
            Serial.println("Raw response:");
            Serial.println(response);
        }
    }
    else
    {
        Serial.printf("❌ Wit.ai HTTP error: %d\n", httpResponseCode);
        Serial.println(http.errorToString(httpResponseCode));
    }

    http.end();
    return "";
}


// =====================================================
//                    GEMINI
// =====================================================

String askGemini(
    const String& question
)
{
    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        Serial.println(
            "❌ WiFi disconnected."
        );

        return "";
    }


    Serial.println();
    Serial.println(
        "🧠 Sending text to Gemini..."
    );


    WiFiClientSecure client;

    client.setInsecure();

    client.setTimeout(60000);


    HTTPClient https;


    String url =
        "https://generativelanguage.googleapis.com/v1beta/models/" +
        String(geminiModel) +
        ":generateContent?key=" +
        String(geminiApiKey);


    if (
        !https.begin(
            client,
            url
        )
    )
    {
        Serial.println(
            "❌ Could not connect to Gemini."
        );

        return "";
    }


    https.setTimeout(60000);


    https.addHeader(
        "Content-Type",
        "application/json"
    );


    // =================================================
    // GEMINI REQUEST JSON
    // =================================================

    JsonDocument requestDoc;

    JsonArray contents = requestDoc["contents"].to<JsonArray>();
    JsonObject content = contents.add<JsonObject>();
    content["role"] = "user";

    JsonArray parts = content["parts"].to<JsonArray>();
    JsonObject part = parts.add<JsonObject>();

    part["text"] =
        question +
        "\n\nAnswer briefly and naturally. "
        "Your response will be converted to speech. "
        "Do not use markdown, bullet points, tables, "
        "emojis, or special formatting. "
        "Keep the answer concise.";

    // Generation configuration
    JsonObject generationConfig = requestDoc["generationConfig"].to<JsonObject>();
    generationConfig["maxOutputTokens"] = 600;

    String requestBody;
    serializeJson(requestDoc, requestBody);

    Serial.println("Sending request...");

    int httpCode = https.POST(requestBody);

    if (httpCode == HTTP_CODE_OK)
    {
        String payload = https.getString();

        JsonDocument responseDoc;
        DeserializationError error = deserializeJson(responseDoc, payload);

        if (error)
        {
            Serial.print("❌ Gemini JSON error: ");
            Serial.println(error.c_str());
            Serial.println(payload);
            https.end();
            return "";
        }

        const char* rawAnswer = responseDoc["candidates"][0]["content"]["parts"][0]["text"];


        if (
            rawAnswer != nullptr
        )
        {
            String answer =
                String(rawAnswer);


            answer.trim();


            Serial.println();
            Serial.println(
                "🤖 GEMINI RESPONSE"
            );

            Serial.println(
                "--------------------------------"
            );

            Serial.println(
                answer
            );

            Serial.println(
                "--------------------------------"
            );


            https.end();

            return answer;
        }
    }
    else
    {
        Serial.print(
            "❌ Gemini HTTP error: "
        );

        Serial.println(
            httpCode
        );


        Serial.println(
            https.getString()
        );
    }


    https.end();

    return "";
}


// =====================================================
//                  GOOGLE TTS
// =====================================================

std::vector<String> ttsQueue;
bool isSpeaking = false;

// Function to clean text for TTS (removes markdown formatting)
String cleanTextForTTS(const String& input) {
    String clean = "";
    clean.reserve(input.length());

    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '\n' || c == '\r' || c == '\t') {
            clean += ' ';
        } else if (c == '*' || c == '#' || c == '_' || c == '`' || c == '~' ||
                   c == '>' || c == '|' || c == '[' || c == ']' || c == '(' || c == ')') {
            clean += ' ';
        } else {
            if (c == ' ' && clean.length() > 0 && clean[clean.length() - 1] == ' ') {
                continue;
            }
            clean += c;
        }
    }
    clean.trim();
    return clean;
}

// Function to split text into chunks suitable for Google TTS (<= 100 characters)
void queueTTSChunks(const String& text) {
    ttsQueue.clear();
    String cleaned = cleanTextForTTS(text);
    if (cleaned.length() == 0) return;

    const size_t MAX_CHUNK_LEN = 100;
    int start = 0;
    int len = cleaned.length();

    while (start < len) {
        if (len - start <= (int)MAX_CHUNK_LEN) {
            String chunk = cleaned.substring(start);
            chunk.trim();
            if (chunk.length() > 0) {
                ttsQueue.push_back(chunk);
            }
            break;
        }

        int splitAt = -1;
        int maxSearch = start + MAX_CHUNK_LEN;

        // 1. Try finding sentence end (. ! ? ;)
        for (int i = maxSearch; i >= start + 20; i--) {
            char c = cleaned[i];
            if (c == '.' || c == '!' || c == '?' || c == ';') {
                splitAt = i + 1;
                break;
            }
        }

        // 2. If no sentence end, try comma or colon (, :)
        if (splitAt == -1) {
            for (int i = maxSearch; i >= start + 20; i--) {
                char c = cleaned[i];
                if (c == ',' || c == ':') {
                    splitAt = i + 1;
                    break;
                }
            }
        }

        // 3. If no punctuation, split at last space
        if (splitAt == -1) {
            for (int i = maxSearch; i >= start + 10; i--) {
                if (cleaned[i] == ' ') {
                    splitAt = i;
                    break;
                }
            }
        }

        // 4. Fallback: hard cut
        if (splitAt == -1) {
            splitAt = maxSearch;
        }

        String chunk = cleaned.substring(start, splitAt);
        chunk.trim();
        if (chunk.length() > 0) {
            ttsQueue.push_back(chunk);
        }

        start = splitAt;
        while (start < len && cleaned[start] == ' ') {
            start++;
        }
    }
}

void speakText(const String& text)
{
    if (text.length() == 0)
    {
        return;
    }

    Serial.println();
    Serial.println("🔊 Preparing Google TTS playback...");

    queueTTSChunks(text);

    Serial.printf("Split into %d TTS chunks for playback.\n", ttsQueue.size());

    if (!ttsQueue.empty())
    {
        isSpeaking = true;
        Serial.printf("🔊 Playing chunk [1/%d]: %s\n", ttsQueue.size(), ttsQueue[0].c_str());
        bool ok = audio.connecttospeech(ttsQueue[0].c_str(), "en");
        if (!ok) {
            Serial.println("❌ Failed to connect to TTS for first chunk.");
            ttsQueue.erase(ttsQueue.begin());
            if (ttsQueue.empty()) {
                isSpeaking = false;
            }
        }
    }
}


// =====================================================
//                     SETUP
// =====================================================

void setup()
{
    Serial.begin(
        115200
    );


    delay(1000);


    Serial.println();
    Serial.println(
        "=========================================="
    );

    Serial.println(
        " ESP32-S3 VOICE AI ASSISTANT"
    );

    Serial.println(
        " INMP441 → Wit.ai → Gemini → Google TTS"
    );

    Serial.println(
        " → MAX98357A"
    );

    Serial.println(
        "=========================================="
    );


    // Allocate recording buffer (using PSRAM if available)
    #if defined(BOARD_HAS_PSRAM)
    audioBuffer = (uint8_t*)ps_malloc(maxRecordSize);
    #else
    audioBuffer = (uint8_t*)malloc(maxRecordSize);
    #endif

    if (audioBuffer == nullptr)
    {
        // Fallback to regular malloc if ps_malloc fails
        audioBuffer = (uint8_t*)malloc(maxRecordSize);
    }

    if (audioBuffer == nullptr)
    {
        Serial.println("❌ Failed to allocate audio buffer.");
        while (1) {
            delay(1000);
        }
    }


    Serial.printf(
        "Audio buffer: %d bytes\n",
        maxRecordSize
    );


    // =================================================
    // WiFi
    // =================================================

    connectWiFi();


    // =================================================
    // INMP441
    // =================================================

    initMicrophoneI2S();


    // =================================================
    // MAX98357A
    // =================================================

    audio.setPinout(
        I2S_SPK_BCLK,
        I2S_SPK_LRC,
        I2S_SPK_DOUT
    );


    audio.setVolume(21);
    Serial.println();
    Serial.println("==========================================");

    Serial.println(" SYSTEM READY");

    Serial.println("==========================================");

    Serial.println(
        "Send '1' and press ENTER to start recording.");
    Serial.println(
        "Send '2' and press ENTER to stop recording."
    );
}


// =====================================================
//                 PROCESS AUDIO PIPELINE
// =====================================================

void processAudioPipeline()
{
    // STEP 1: WIT.AI
    String userText = sendToWitAI();

    if (userText.length() == 0)
    {
        Serial.println("❌ No speech recognized.");
        Serial.println("Send '1' and press ENTER to try again.");
        return;
    }

    // STEP 2: GEMINI
    String answer = askGemini(userText);

    if (answer.length() == 0)
    {
        Serial.println("❌ Gemini returned no answer.");
        Serial.println("Send '1' and press ENTER to try again.");
        return;
    }

    // STEP 3: GOOGLE TTS
    speakText(answer);
}


// =====================================================
//                      LOOP
// =====================================================

void loop()
{
    // Keep Google TTS audio processing alive
    audio.loop();

    // Check if TTS is active and advance chunks when the current one finishes
    if (isSpeaking)
    {
        if (!audio.isRunning())
        {
            if (!ttsQueue.empty())
            {
                ttsQueue.erase(ttsQueue.begin());
            }

            if (!ttsQueue.empty())
            {
                Serial.printf("🔊 Playing next chunk (%d remaining): %s\n", (int)ttsQueue.size(), ttsQueue[0].c_str());
                bool ok = audio.connecttospeech(ttsQueue[0].c_str(), "en");
                if (!ok) {
                    Serial.println("❌ Failed to connect for chunk, skipping...");
                    ttsQueue.erase(ttsQueue.begin());
                    if (ttsQueue.empty()) {
                        isSpeaking = false;
                    }
                }
                delay(10);
            }
            else
            {
                isSpeaking = false;
                Serial.println("\n🔊 Finished speaking entire response.");
                Serial.println("Ready for next question.");
                Serial.println("Send '1' and press ENTER to record again.\n");
            }
        }
    }

    // Handle Serial commands
    if (Serial.available() > 0)
    {
        String inputStr = Serial.readStringUntil('\n');
        inputStr.trim();

        if (inputStr.length() > 0)
        {
            char cmd = inputStr.charAt(0);

            if (cmd == '1')
            {
                if (isSpeaking)
                {
                    // Interrupt speech if new recording is triggered
                    audio.stopSong();
                    ttsQueue.clear();
                    isSpeaking = false;
                }

                if (!isRecording)
                {
                    startRecording();
                }
                else
                {
                    Serial.println("⚠ Already recording! Send '2' to stop.");
                }
            }
            else if (cmd == '2')
            {
                if (isRecording)
                {
                    stopRecording();
                    processAudioPipeline();
                }
                else
                {
                    Serial.println("⚠ Not currently recording. Send '1' to start.");
                }
            }
        }
    }

    // Record audio while isRecording is true
    if (isRecording)
    {
        recordAudio();
    }

    delay(1);
}


// =====================================================
//              AUDIO LIBRARY CALLBACK
// =====================================================

void audio_info(
    const char *info
)
{
    Serial.print(
        "[Audio] "
    );

    Serial.println(
        info
    );
}


void audio_eof_speech(
    const char *info
)
{
    Serial.println();
    Serial.println(
        "🔊 Finished speaking."
    );

    Serial.println(
        "Ready for next question."
    );

    Serial.println(
        "Send '1' and press ENTER to record again."
    );
}