 #include <Arduino.h>
  #include <WiFi.h>
  #include "Audio.h"

  // Wi-Fi Credentials
  const char* ssid = "JioFiber-PUl8o";
  const char* password = "ramesh-Gayathri,251114";

  // MAX98357A I2S Pin Definitions (Change if using different GPIO pins)
  #define I2S_BCLK 6   // Bit Clock (BCLK)
  #define I2S_LRC  11  // Word Select / Left-Right Clock (LRC / WS)
  #define I2S_DOUT 12  // Serial Data / Data In (DIN)

  Audio audio;

  void setup() {
      Serial.begin(115200);
      delay(1000);

      Serial.println("\n=================================");
      Serial.println("  ESP32 Google TTS via MAX98357A  ");
      Serial.println("=================================");

      // Connect to Wi-Fi
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, password);

      Serial.print("Connecting to Wi-Fi");
      while (WiFi.status() != WL_CONNECTED) {
          delay(500);
          Serial.print(".");
      }
      Serial.println("\n[WiFi] Connected!");
      Serial.print("[WiFi] IP Address: ");
      Serial.println(WiFi.localIP());

      // Configure I2S pinout for MAX98357A
      audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);

      // Set volume level (range: 0 to 21)
      audio.setVolume(20);

      Serial.println("\n[Ready] Type text in the Serial Monitor and press ENTER to speak:");
  }

  void loop() {
      // Keep audio stream alive and processing non-blocking I2S buffers
      audio.loop();

      // Check if user has entered text in the Serial Monitor
      if (Serial.available()) {
          String input = Serial.readStringUntil('\n');
          input.trim(); // Remove leading/trailing spaces and newlines

          if (input.length() > 0) {
              Serial.print("\n[Speaking]: ");
              Serial.println(input);

              // Connect to Google TTS (language: "en" for English)
              audio.connecttospeech(input.c_str(), "en-IN");
          }
      }
  }

  // Audio library status callbacks
  void audio_info(const char *info) {
      Serial.print("[Audio Info] ");
      Serial.println(info);
  }

  void audio_eof_speech(const char *info) {
      Serial.println("\n[Done] Finished speaking. Enter next text:");
  }