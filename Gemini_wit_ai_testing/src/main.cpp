#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WitAITTS.h>
// =====================================================
//                    WIFI
// =====================================================
const char* ssid     = "WiFi name";  // Your Wi-Fi SSID
const char* password = "Wi-Fi password";  // Your Wi-Fi password
// =====================================================
//                    GEMINI
// =====================================================
const char* geminiApiKey = "YOUR GEMINI API KEY";  // Your Gemini API key
const char* model = "gemini-3.5-flash";
const int maxTokens = 300;
// =====================================================
//                    WIT.AI
// =====================================================
// IMPORTANT:
// This MUST be your Wit.ai Server Access Token.
// It is NOT your Gemini API key.
const char* witToken = "YOUR WIT.AI SERVER ACCESS TOKEN";
// =====================================================
//                    MAX98357A
// =====================================================
// Your original WitAITTS sketch uses these pins.
#define CUSTOM_BCLK 7
#define CUSTOM_LRC  8
#define CUSTOM_DIN  9
WitAITTS tts(CUSTOM_BCLK, CUSTOM_LRC, CUSTOM_DIN);
// =====================================================
//                 ASK GEMINI
// =====================================================
String askGemini(const String& question) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60000);
  HTTPClient https;
  String url =
    "https://generativelanguage.googleapis.com/v1beta/models/" +
    String(model) +
    ":generateContent?key=" +
    String(geminiApiKey);
  if (!https.begin(client, url)) {
    Serial.println("❌ Could not connect to Gemini.");
    return "";
  }
  https.setTimeout(60000);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("User-Agent", "ESP32-S3");
  // ---------------------------------------------------
  // Create Gemini request
  // ---------------------------------------------------
  StaticJsonDocument<1024> requestDoc;
  JsonArray contents =
    requestDoc.createNestedArray("contents");
  JsonObject content =
    contents.createNestedObject();
  JsonArray parts =
    content.createNestedArray("parts");
  JsonObject part =
    parts.createNestedObject();
  part["text"] =
    question +
    "\n\nAnswer briefly and naturally. "
    "Your answer will be converted to speech, "
    "so do not use markdown, tables, bullet points, "
    "or unnecessary formatting. Keep the answer "
    "under 250 characters if possible.";
  JsonObject generationConfig =
    requestDoc.createNestedObject("generationConfig");
  generationConfig["maxOutputTokens"] = maxTokens;
  String requestBody;
  serializeJson(requestDoc, requestBody);
  Serial.println();
  Serial.println("========================================");
  Serial.println("Sending question to Gemini...");
  Serial.println("========================================");
  int httpCode =
    https.POST(requestBody);
  // ---------------------------------------------------
  // Gemini success
  // ---------------------------------------------------
  if (httpCode == HTTP_CODE_OK) {
    String payload =
      https.getString();
    DynamicJsonDocument responseDoc(8192);
    DeserializationError error =
      deserializeJson(responseDoc, payload);
    if (error) {
      Serial.print("❌ JSON parsing error: ");
      Serial.println(error.c_str());
      https.end();
      return "";
    }
    const char* rawAnswer =
      responseDoc["candidates"][0]
                 ["content"]
                 ["parts"][0]
                 ["text"];
    if (rawAnswer != nullptr) {
      String answer =
        String(rawAnswer);
      answer.trim();
      Serial.println();
      Serial.println("🧠 GEMINI RESPONSE");
      Serial.println("----------------------------------------");
      Serial.println(answer);
      Serial.println("----------------------------------------");
      https.end();
      return answer;
    }
    const char* finishReason =
      responseDoc["candidates"][0]
                 ["finishReason"];
    Serial.println("⚠️ Gemini returned no text.");
    if (finishReason != nullptr) {
      Serial.print("Finish reason: ");
      Serial.println(finishReason);
    }
  }
  // ---------------------------------------------------
  // Gemini error
  // ---------------------------------------------------
  else {
    Serial.print("❌ Gemini HTTP Error: ");
    Serial.println(httpCode);
    String errorResponse =
      https.getString();
    if (errorResponse.length() > 0) {
      Serial.println();
      Serial.println("Gemini error response:");
      Serial.println(errorResponse);
    }
  }
  https.end();
  return "";
}
// =====================================================
//                 SEND TO WIT.AI
// =====================================================
void speakAnswer(String answer) {
  answer.trim();
  if (answer.length() == 0) {
    Serial.println("❌ Nothing to speak.");
    return;
  }
  // ---------------------------------------------------
  // WitAITTS maximum = 280 characters
  // ---------------------------------------------------
  if (answer.length() > 280) {
    Serial.println();
    Serial.println("⚠️ Gemini response is longer than 280 characters.");
    Serial.println("Truncating for Wit.ai...");
    answer =
      answer.substring(0, 280);
    // Try to avoid ending halfway through a word.
    int lastSpace =
      answer.lastIndexOf(' ');
    if (lastSpace > 200) {
      answer =
        answer.substring(0, lastSpace);
    }
    answer += ".";
  }
  Serial.println();
  Serial.println("========================================");
  Serial.println("🔊 Sending response to Wit.ai...");
  Serial.println("========================================");
  Serial.println(answer);
  Serial.println();
  bool result =
    tts.speak(answer);
  if (result) {
    Serial.println("✓ Wit.ai TTS request accepted.");
  } else {
    Serial.println("❌ Wit.ai TTS request failed.");
  }
}
// =====================================================
//                         SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32-S3 GEMINI + WIT.AI ASSISTANT");
  Serial.println("========================================");
  // ---------------------------------------------------
  // WiFi
  // ---------------------------------------------------
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println();
  Serial.println("Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("✓ Wi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  // ---------------------------------------------------
  // Wit.ai
  // ---------------------------------------------------
  Serial.println();
  Serial.println("Initializing Wit.ai...");
  tts.setDebugLevel(DEBUG_INFO);
  if (tts.begin(
        ssid,
        password,
        witToken
      )) {
    Serial.println("✓ Wit.ai TTS ready!");
    tts.setVoice("wit$Rubie");  // Try Rebecca - clearer female voice
    tts.setStyle("soft");         // Softer articulation
    tts.setSpeed(97);             // Slightly slower for clarity
    tts.setPitch(100);
    tts.setGain(2);
    tts.printConfig();
  } else {
    Serial.println();
    Serial.println("❌ Wit.ai initialization failed!");
    Serial.println("Check your Wit.ai Server Access Token.");
  }
  // ---------------------------------------------------
  // Ready
  // ---------------------------------------------------
  Serial.println();
  Serial.println("========================================");
  Serial.println("             SYSTEM READY");
  Serial.println("========================================");
  Serial.println();
  Serial.println("Type a question in Serial Monitor.");
  Serial.println("Press Enter.");
  Serial.println();
}
// =====================================================
//                          LOOP
// =====================================================
void loop() {
  // ---------------------------------------------------
  // IMPORTANT:
  // Keep this running continuously.
  // It handles Wit.ai audio streaming.
  // ---------------------------------------------------
  tts.loop();
  // ---------------------------------------------------
  // Check Serial Monitor
  // ---------------------------------------------------
  if (Serial.available()) {
    String question =
      Serial.readStringUntil('\n');
    question.trim();
    if (question.length() == 0) {
      return;
    }
    Serial.println();
    Serial.println("========================================");
    Serial.print("QUESTION: ");
    Serial.println(question);
    Serial.println("========================================");
    // -------------------------------------------------
    // Gemini
    // -------------------------------------------------
    String answer =
      askGemini(question);
    // -------------------------------------------------
    // Wit.ai
    // -------------------------------------------------
    if (answer.length() > 0) {
      speakAnswer(answer);
    }
    Serial.println();
    Serial.println("----------------------------------------");
    Serial.println("Ready for another question.");
    Serial.println("----------------------------------------");
  }
}
