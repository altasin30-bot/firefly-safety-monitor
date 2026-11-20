/*
  Firefly: Gas & Fire Safety Monitor (ESP32)

  Reads MQ-2, MQ-7, MQ-135 and TMP36 sensors, raises a local alarm
  (LEDs + buzzer) and sends live data to Firebase Realtime Database
  using the REST API (no extra libraries needed).

  Before uploading to the ESP32, edit the three values in the
  "CHANGE THESE" section below.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ===== CHANGE THESE =====
const char* ssid1 = "YOUR_WIFI_NAME";
const char* pass1 = "YOUR_WIFI_PASSWORD";
// Do not put a "/" at the end
const String FIREBASE_URL = "https://YOUR-PROJECT-default-rtdb.region.firebasedatabase.app";
// ========================

// --- Pins ---
const int pinTMP36  = 36;
const int pinMQ2    = 39;
const int pinMQ7    = 34;
const int pinMQ135  = 35;
const int pinGreenLED = 25;
const int pinAmberLED = 26;
const int pinRedLED   = 27;
const int pinBuzzer   = 14;

// --- Thresholds ---
const float TEMP_WARNING  = 40.0;  // °C
const float TEMP_CRITICAL = 55.0;  // °C
const int   GAS_WARNING   = 1000;  // mV
const int   GAS_CRITICAL  = 2000;  // mV

// Classify a gas sensor reading as LOW, MEDIUM or HIGH
String levelGas(int mv) {
  if (mv > GAS_CRITICAL) return "HIGH";
  if (mv > GAS_WARNING)  return "MEDIUM";
  return "LOW";
}

// Classify a temperature reading as LOW, MEDIUM or HIGH
String levelTemp(float c) {
  if (c >= TEMP_CRITICAL) return "HIGH";
  if (c >= TEMP_WARNING)  return "MEDIUM";
  return "LOW";
}

// Connect to Wi-Fi (gives up after about 30 seconds and continues offline)
void setup_wifi() {
  Serial.println("\nConnecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid1, pass1);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempts > 60) {
      Serial.println("\nWiFi failed. Running in offline alarm mode.");
      return;
    }
  }
  Serial.println("\nWiFi connected!");
  Serial.println(WiFi.localIP());
}

// Send the JSON data to Firebase Realtime Database (REST API)
bool sendToFirebase(const String& json) {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();          // skip certificate check (simple setup)

  HTTPClient http;
  http.setTimeout(5000);
  String url = FIREBASE_URL + "/firefly/latest.json";

  if (!http.begin(secureClient, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(json);           // PUT replaces the old data at this path
  http.end();

  Serial.printf("Firebase HTTP: %d\n", code);
  return code == 200;
}

void setup() {
  Serial.begin(115200);

  // Set LED and buzzer pins as outputs
  pinMode(pinGreenLED, OUTPUT);
  pinMode(pinAmberLED, OUTPUT);
  pinMode(pinRedLED, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);

  // Turn everything off at start
  digitalWrite(pinGreenLED, LOW);
  digitalWrite(pinAmberLED, LOW);
  digitalWrite(pinRedLED, LOW);
  digitalWrite(pinBuzzer, LOW);

  analogReadResolution(12);            // 12-bit ADC (0 - 4095)
  setup_wifi();
}

void loop() {
  // 1. Read sensors (in millivolts)
  int mvTMP   = analogReadMilliVolts(pinTMP36);
  int mvMQ2   = analogReadMilliVolts(pinMQ2);
  int mvMQ7   = analogReadMilliVolts(pinMQ7);
  int mvMQ135 = analogReadMilliVolts(pinMQ135);

  // TMP36: 500 mV = 0 °C, 10 mV per °C
  float tempC = (mvTMP - 500) / 10.0;

  // Classify each reading
  String lvlTemp  = levelTemp(tempC);
  String lvlMQ2   = levelGas(mvMQ2);
  String lvlMQ7   = levelGas(mvMQ7);
  String lvlMQ135 = levelGas(mvMQ135);

  // Print readings to the Serial Monitor
  Serial.printf("Temp: %.1f C | MQ2: %d mV | MQ7: %d mV | MQ135: %d mV\n",
                tempC, mvMQ2, mvMQ7, mvMQ135);

  // 2. Decide the overall status
  String status = "SAFE";
  if (lvlTemp == "HIGH" || lvlMQ2 == "HIGH" || lvlMQ7 == "HIGH") {
    status = "RISKY";
  } else if (lvlTemp == "MEDIUM" || lvlMQ2 == "MEDIUM" || lvlMQ7 == "MEDIUM") {
    status = "CAUTION";
  }

  // 3. Local alarm (works even without Wi-Fi)
  digitalWrite(pinGreenLED, status == "SAFE");
  digitalWrite(pinAmberLED, status == "CAUTION");
  digitalWrite(pinRedLED,   status == "RISKY");
  digitalWrite(pinBuzzer,   status == "RISKY");

  // 4. Send data to Firebase (only if Wi-Fi is connected)
  if (WiFi.status() == WL_CONNECTED) {
    String json = "{";
    json += "\"status\":\"" + status + "\",";
    json += "\"ts\":" + String(millis()) + ",";   // changes every update, used by the dashboard to detect offline
    json += "\"mq2\":{\"mv\":"   + String(mvMQ2)   + ",\"level\":\"" + lvlMQ2   + "\"},";
    json += "\"mq7\":{\"mv\":"   + String(mvMQ7)   + ",\"level\":\"" + lvlMQ7   + "\"},";
    json += "\"mq135\":{\"mv\":" + String(mvMQ135) + ",\"level\":\"" + lvlMQ135 + "\"},";
    json += "\"temp\":{\"c\":"   + String(tempC, 1) + ",\"level\":\"" + lvlTemp  + "\"}";
    json += "}";
    sendToFirebase(json);
  } else {
    WiFi.reconnect();   // Wi-Fi dropped: try to connect again
  }

  delay(1500);
}
