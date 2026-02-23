#include <Arduino.h>
#include <WiFi.h>

#ifndef LED_PIN
#define LED_PIN 2
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

static const uint32_t SCAN_INTERVAL_MS = 15000;
static const uint32_t HEARTBEAT_MS = 1000;

unsigned long lastScanMs = 0;
unsigned long lastBlinkMs = 0;
bool ledState = false;
bool connectAttempted = false;
bool wifiConnected = false;

void printScanResults() {
  Serial.println("[WIFI] Starting scan...");
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    Serial.println("[WIFI] Scan failed");
    return;
  }

  Serial.printf("[WIFI] Found %d network(s)\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  %2d) SSID=%s RSSI=%d dBm CH=%d ENC=%d\n",
                  i + 1,
                  WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i),
                  WiFi.channel(i),
                  WiFi.encryptionType(i));
  }
  WiFi.scanDelete();
}

void tryConnectOnce() {
  if (connectAttempted) {
    return;
  }
  connectAttempted = true;

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[WIFI] WIFI_SSID not set; running scan-only mode.");
    return;
  }

  Serial.printf("[WIFI] Connecting to SSID '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  const uint32_t timeoutMs = 20000;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("[WIFI] CONNECTED");
    Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WIFI] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("[WIFI] DNS: %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.printf("[WIFI] CONNECT FAILED, status=%d\n", WiFi.status());
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.begin(115200);
  delay(300);

  Serial.println("WIFI_TEST_START");
  Serial.printf("[WIFI] MAC: %s\n", WiFi.macAddress().c_str());

  WiFi.mode(WIFI_STA);
  delay(100);

  printScanResults();
  lastScanMs = millis();

  tryConnectOnce();
}

void loop() {
  unsigned long now = millis();

  if ((now - lastBlinkMs) >= HEARTBEAT_MS) {
    lastBlinkMs = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    Serial.printf("[HB] LED %s  WIFI=%s  RSSI=%d\n",
                  ledState ? "ON" : "OFF",
                  (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "DISCONNECTED",
                  (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0);
  }

  if ((now - lastScanMs) >= SCAN_INTERVAL_MS) {
    lastScanMs = now;
    if (WiFi.status() != WL_CONNECTED) {
      printScanResults();
    }
  }

  if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("[WIFI] Reconnected");
    Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
  }
}
