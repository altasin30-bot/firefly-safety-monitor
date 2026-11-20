#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

WiFiMulti wifiMulti;

// ===== EITA CHANGE KORO =====
const char* ssid1 = "Narzo 50";
const char* pass1 = "YOUR_WIFI_PASSWORD";
// dont use "/" in last
const String FIREBASE_URL = "https://PROJECT-default-rtdb.asia-southeast1.firebasedatabase.app";

// =============================

// --- Pins ---
const int pinTMP36  = 36;
const int pinMQ2    = 39;
const int pinMQ7    = 34;
const int pinMQ135  = 35;
const int pinGreenLED = 25;
const int pinAmberLED = 26;
const int pinRedLED   = 27;
const int pinBuzzer   = 14;

// --- Thresholds (millivolt) ---
const float TEMP_WARNING  = 40.0;
const float TEMP_CRITICAL = 55.0;
const int   GAS_WARNING   = 1000;  // mV
const int   GAS_CRITICAL  = 2000;  // mV

String levelGas(int mv) {
  if (mv > GAS_CRITICAL) return "HIGH";
  if (mv > GAS_WARNING)  return "MEDIUM";
  return "LOW";
}

String levelTemp(float c) {
  if (c >= TEMP_CRITICAL) return "HIGH";
  if (c >= TEMP_WARNING)  return "MEDIUM";
  return "LOW";
}

void setup_wifi() {
  Serial.println("\nConnecting to WiFi...");
  wifiMulti.addAP(ssid1, pass1);

  int attempts = 0;
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempts > 60) {
      Serial.println("\nWiFi failed. Offline alarm mode.");
      return;
    }
  }
  Serial.println("\nWiFi connected!");
  Serial.println(WiFi.localIP());
}

bool sendToFirebase(const String& json) {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();          // certificate check bondho (simple setup)

  HTTPClient http;
  http.setTimeout(5000);
  String url = FIREBASE_URL + "/firefly/latest.json";

  if (!http.begin(secureClient, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(json);           // PUT = purano data replace kore
  http.end();

  Serial.printf("Firebase HTTP: %d\n", code);
  return code == 200;
}

void setup() {
  Serial.begin(115200);

  pinMode(pinGreenLED, OUTPUT);
  pinMode(pinAmberLED, OUTPUT);
  pinMode(pinRedLED, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);

  digitalWrite(pinGreenLED, LOW);
  digitalWrite(pinAmberLED, LOW);
  digitalWrite(pinRedLED, LOW);
  digitalWrite(pinBuzzer, LOW);

  analogReadResolution(12);
  setup_wifi();
}

void loop() {
  // 1. Sensor read (millivolt e)
  int mvTMP  = analogReadMilliVolts(pinTMP36);
  int mvMQ2  = analogReadMilliVolts(pinMQ2);
  int mvMQ7  = analogReadMilliVolts(pinMQ7);
  int mvMQ135 = analogReadMilliVolts(pinMQ135);

  float tempC = (mvTMP - 500) / 10.0;   // TMP36: 500mV = 0°C, 10mV per °C

  String lvlTemp  = levelTemp(tempC);
  String lvlMQ2   = levelGas(mvMQ2);
  String lvlMQ7   = levelGas(mvMQ7);
  String lvlMQ135 = levelGas(mvMQ135);

  Serial.printf("Temp: %.1f C | MQ2: %d mV | MQ7: %d mV | MQ135: %d mV\n",
                tempC, mvMQ2, mvMQ7, mvMQ135);

  // 2. Overall status
  String status = "SAFE";
  if (lvlTemp == "HIGH" || lvlMQ2 == "HIGH" || lvlMQ7 == "HIGH") {
    status = "RISKY";
  } else if (lvlTemp == "MEDIUM" || lvlMQ2 == "MEDIUM" || lvlMQ7 == "MEDIUM") {
    status = "CAUTION";
  }

  // 3. Local alarm (WiFi na thakleo kaj kore)
  digitalWrite(pinGreenLED, status == "SAFE");
  digitalWrite(pinAmberLED, status == "CAUTION");
  digitalWrite(pinRedLED,   status == "RISKY");
  digitalWrite(pinBuzzer,   status == "RISKY");

  // 4. Firebase e pathano
  if (wifiMulti.run() == WL_CONNECTED) {
    String json = "{";
    json += "\"status\":\"" + status + "\",";
    json += "\"ts\":" + String(millis()) + ",";
    json += "\"mq2\":{\"mv\":"   + String(mvMQ2)   + ",\"level\":\"" + lvlMQ2   + "\"},";
    json += "\"mq7\":{\"mv\":"   + String(mvMQ7)   + ",\"level\":\"" + lvlMQ7   + "\"},";
    json += "\"mq135\":{\"mv\":" + String(mvMQ135) + ",\"level\":\"" + lvlMQ135 + "\"},";
    json += "\"temp\":{\"c\":"   + String(tempC, 1) + ",\"level\":\"" + lvlTemp  + "\"}";
    json += "}";
    sendToFirebase(json);
  }

  delay(1500);
}