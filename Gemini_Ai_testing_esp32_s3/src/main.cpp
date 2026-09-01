#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= USER CONFIGURATION =================
const char* ssid     = "Wi-Fi-Name";
const char* password = "Wi-fi-password";
const char* apiKey   = "API-KEY";  // Replace with your actual Gemini API key

// Testing with 3.5-flash to verify network & latency behavior
const char* model    = "gemini-3.5-flash"; 
const int maxTokens  = 4000;
// ======================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  // Keep Wi-Fi modem fully awake
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("\nConnecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ Wi-Fi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("\n✍️  Type your question in the Serial Monitor and press Enter:");
}

void askGemini(const String& prompt) {
  WiFiClientSecure client;
  client.setInsecure();          // Skip TLS certificate validation
  client.setTimeout(60000);      // 60-second socket timeout

  HTTPClient https;
  String url = "https://generativelanguage.googleapis.com/v1beta/models/" + String(model) + ":generateContent?key=" + String(apiKey);

  if (!https.begin(client, url)) {
    Serial.println("❌ Failed to initiate HTTPS connection.");
    return;
  }

  https.setTimeout(60000);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("User-Agent", "ESP32-Client");

  // Construct JSON request payload
  StaticJsonDocument<512> requestDoc;
  JsonArray contents = requestDoc.createNestedArray("contents");
  JsonObject partObj = contents.createNestedObject().createNestedArray("parts").createNestedObject();
  partObj["text"] = prompt;

  JsonObject genConfig = requestDoc.createNestedObject("generationConfig");
  genConfig["maxOutputTokens"] = maxTokens;

  String requestBody;
  serializeJson(requestDoc, requestBody);

  Serial.println("\n🚀 Sending request to Gemini...");
  int httpCode = https.POST(requestBody);

  if (httpCode == HTTP_CODE_OK) {
    String payload = https.getString();

    // 8 KB buffer is ideal for standard text responses on the ESP32-S3
    DynamicJsonDocument responseDoc(8192);
    DeserializationError error = deserializeJson(responseDoc, payload);

    if (error) {
      Serial.print("❌ JSON Parsing Error: ");
      Serial.println(error.c_str());
    } else {
      const char* rawAnswer = responseDoc["candidates"][0]["content"]["parts"][0]["text"];

      if (rawAnswer) {
        String answer = String(rawAnswer);
        answer.trim();

        Serial.println("\n🧠 Response:");
        Serial.println("----------------------------------------");
        Serial.println(answer);
        Serial.println("----------------------------------------");
      } else {
        const char* finishReason = responseDoc["candidates"][0]["finishReason"];
        Serial.print("⚠️ No text returned. Finish reason: ");
        Serial.println(finishReason ? finishReason : "Unknown");
      }
    }
  } else {
    Serial.print("❌ HTTP Error Code: ");
    Serial.println(httpCode);
    Serial.print("Error Message: ");
    Serial.println(https.errorToString(httpCode).c_str());
  }
  https.end();
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() > 0) {
      Serial.print("\nQuestion: ");
      Serial.println(input);
      askGemini(input);
      Serial.println("\n✍️  Type another question and press Enter:");
    }
  }
}