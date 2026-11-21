#include <Arduino.h>
#include <ESP32WifiManager.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("ESP32WifiManager Demo");

    // Lang nghe thay doi trang thai
    ESP32WifiManager::setOnStateChanged([](WifiState state) {
        Serial.printf("[State Change] %s\n", ESP32WifiManager::getStateString());
    });

    // Khoi tao va thu ket noi voi mang mac dinh (neu chua luu)
    ESP32WifiManager::init("MyWiFiSSID", "MyPassword123");

    // Quet mang bat dong bo
    ESP32WifiManager::startScan();
}

void loop() {
    ESP32WifiManager::update();

    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 3000) {
        lastPrint = millis();

        if (ESP32WifiManager::isScanning()) {
            Serial.println("WiFi scanning in progress...");
        } else {
            auto results = ESP32WifiManager::getScanResults();
            Serial.printf("Scanned %u networks:\n", (unsigned int)results.size());
            for (const auto& item : results) {
                Serial.printf(" - %-20s | RSSI: %3d dBm | Encrypted: %s\n",
                              item.ssid.c_str(), item.rssi, item.isEncrypted ? "YES" : "NO");
            }
        }

        if (ESP32WifiManager::isConnected()) {
            Serial.printf("[WiFi] Connected to %s | IP: %s | Signal: %d%%\n",
                          ESP32WifiManager::getConnectedSSID().c_str(),
                          ESP32WifiManager::getIPAddress().c_str(),
                          ESP32WifiManager::getSignalPercent());
        }
    }

    delay(10);
}
