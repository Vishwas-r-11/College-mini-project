#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Audio.h" //[cite: 1]

// =====================================================
//                    WIFI & API KEYS
// =====================================================
const char* ssid         = "WiFi name"; 
const char* password     = "WiFi password"; 
const char* geminiApiKey = "YOUR_GEMINI_API_KEY";
const char* model        = "gemini-3.5-flash"; 
const int maxTokens      = 2000;

// =====================================================
//                 I2S AUDIO (MAX98357A)
// =====================================================
#define I2S_BCLK 6   // Bit Clock[cite: 1]
#define I2S_LRC  11  // Word Select[cite: 1]
#define I2S_DOUT 12  // Serial Data[cite: 1]

Audio audio; //[cite: 1]

// =====================================================
//                 ASK GEMINI
// =====================================================
String askGemini(const String& question) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60000);
  HTTPClient https;
  String url = "https://generativelanguage.googleapis.com/v1beta/models/" + String(model) + ":generateContent?key=" + String(geminiApiKey);

  if (!https.begin(client, url)) {
    Serial.println("❌ Could not connect to Gemini.");
    return "";
  }
  https.setTimeout(60000);
  https.addHeader("Content-Type", "application/json");

  // Create JSON Payload
  StaticJsonDocument<1024> requestDoc;
  JsonArray contents = requestDoc.createNestedArray("contents");
  JsonObject content = contents.createNestedObject();
  JsonArray parts = content.createNestedArray("parts");
  JsonObject part = parts.createNestedObject();
  
  // Prompt instruction keeps answer concise for TTS
  part["text"] = question + "\n\nAnswer briefly and naturally. Your answer will be converted to speech, so do not use markdown, tables, bullet points, or unnecessary formatting. Keep the answer under 200 characters if possible.";
  
  JsonObject generationConfig = requestDoc.createNestedObject("generationConfig");
  generationConfig["maxOutputTokens"] = maxTokens;

  String requestBody;
  serializeJson(requestDoc, requestBody);
  
  Serial.println("\n========================================");
  Serial.println("Sending question to Gemini...");
  Serial.println("========================================");

  int httpCode = https.POST(requestBody);

  if (httpCode == HTTP_CODE_OK) {
    String payload = https.getString();
    DynamicJsonDocument responseDoc(8192);
    DeserializationError error = deserializeJson(responseDoc, payload);
    
    if (error) {
      Serial.print("❌ JSON parsing error: ");
      Serial.println(error.c_str());
      https.end();
      return "";
    }
    
    const char* rawAnswer = responseDoc["candidates"][0]["content"]["parts"][0]["text"];
    if (rawAnswer != nullptr) {
      String answer = String(rawAnswer);
      answer.trim();
      
      // Display Gemini response on Serial Monitor
      Serial.println("\n🧠 GEMINI RESPONSE");
      Serial.println("----------------------------------------");
      Serial.println(answer);
      Serial.println("----------------------------------------");
      
      https.end();
      return answer;
    }
  } else {
    Serial.print("❌ Gemini HTTP Error: ");
    Serial.println(httpCode);
    Serial.println(https.getString());
  }
  https.end();
  return "";
}

// =====================================================
//                    SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=================================");
  Serial.println(" ESP32 GEMINI + GOOGLE TTS "); //[cite: 1]
  Serial.println("=================================");

  // Connect to Wi-Fi
  WiFi.disconnect(true); //[cite: 1]
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✓ Wi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Configure Audio Library
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT); //[cite: 1]
  audio.setVolume(30); //[cite: 1]

  Serial.println("\n========================================");
  Serial.println("           SYSTEM READY");
  Serial.println("========================================");
  Serial.println("Type a question in Serial Monitor and press Enter.");
}

// =====================================================
//                    LOOP
// =====================================================
void loop() {
  // Keep audio stream alive and processing non-blocking I2S buffers[cite: 1]
  audio.loop(); 

  // Check for User Input
  if (Serial.available()) {
    String question = Serial.readStringUntil('\n');
    question.trim();
    
    if (question.length() > 0) {
      Serial.print("\nQUESTION: ");
      Serial.println(question);
      
      // Get AI Answer
      String answer = askGemini(question);
      
      // Speak Answer
      if (answer.length() > 0) {
        Serial.print("\n[Speaking]: "); //[cite: 1]
        Serial.println("Initiating Google TTS...");
        
        // Connect to Google TTS (language: "en-IN")[cite: 1]
        audio.connecttospeech(answer.c_str(), "en"); 
      }
    }
  }
}

// =====================================================
//            AUDIO LIBRARY CALLBACKS
// =====================================================
void audio_info(const char *info) {
    Serial.print("[Audio Info] "); //[cite: 1]
    Serial.println(info);
}

void audio_eof_speech(const char *info) {
    Serial.println("\n[Done] Finished speaking. Ready for next question."); //[cite: 1]
}