\# College-mini-project

An autonomous, voice-actuated mobile robot powered by ESP32 and Google Gemini AI, featuring full-duplex I2S digital audio streaming, local intent parsing, and tool-calling kinematic motor control.



\# 🤖 VoxBot-32: Gemini-Powered Voice-Actuated Mobile Robot



An embedded Human-Robot Interaction (HRI) platform that bridges edge-level digital signal processing on an \*\*ESP32\*\* with the reasoning capabilities of \*\*Google Gemini\*\*. The robot captures digital voice input, streams audio over Wi-Fi, processes natural language intent using \*\*Gemini Tool/Function Calling\*\*, and executes physical kinematic navigation while synthesizing low-latency spoken responses.



\---



\## 🚀 Key Features



\* \*\*End-to-End Voice Interaction:\*\* Hands-free conversational interface using high-fidelity I2S digital audio hardware (MEMS microphone + Class-D amplifier).

\* \*\*LLM Tool Calling for Locomotion:\*\* Natural language queries are automatically parsed into structured JSON payloads (`direction`, `distance\_cm`, `speed`) to execute motor commands.

\* \*\*Dual-Core Asynchronous Processing:\*\* Audio buffering and network telemetry run across dedicated FreeRTOS tasks to prevent playback stuttering or command drops.

\* \*\*Isolated Power \& Signal Architecture:\*\* Split-rail power supply and common-ground decoupling to shield digital audio lines from high-current motor back-EMF.

\* \*\*Status Telemetry:\*\* Real-time feedback displayed on an I2C OLED screen and an RGB status indicator.



\---



\## 🧠 System Architecture



```text

&#x20; \[ User Voice ]

&#x20;        │

&#x20;        ▼

&#x20; \[ INMP441 Mic (I2S) ] ──► \[ ESP32 Microcontroller ] ──► \[ Wi-Fi Gateway ]

&#x20;                                                                  │

&#x20;                                                                  ▼

&#x20;                                                      \[ Cloud / Python Host ]

&#x20;                                                                  │

&#x20;                                                ┌─────────────────┴─────────────────┐

&#x20;                                                ▼                                   ▼

&#x20;                                       \[ Gemini 2.5 / 3.0 API ]             \[ Deepgram / STT ]

&#x20;                                       (Intent + Tool Calling)                      │

&#x20;                                                │                                   ▼

&#x20;                                                ├───────────────────────► \[ Structured JSON ]

&#x20;                                                ▼                                   │

&#x20;                                        \[ Google / Piper TTS ]                      ▼

&#x20;                                                │                           \[ Motor Control ]

&#x20;                                                ▼                         (TB6612FNG / Motors)

&#x20;                                       \[ Audio Byte Stream ]                        │

&#x20;                                                │                                   ▼

&#x20;                                                ▼                           \[ Physical Motion ]

* &#x20; \[ Speaker ] ◄── \[ MAX98357A Amp (I2S) ] ◄──────┘

