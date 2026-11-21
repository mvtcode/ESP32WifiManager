#include "ESP32WifiManager.h"
#include <Preferences.h>
#include <algorithm>

#if defined(ENABLE_SERIAL_LOG)
  #define WIFI_LOG_I(fmt, ...) Serial.printf("[WiFi][INFO] " fmt "\n", ##__VA_ARGS__)
  #define WIFI_LOG_W(fmt, ...) Serial.printf("[WiFi][WARN] " fmt "\n", ##__VA_ARGS__)
  #define WIFI_LOG_E(fmt, ...) Serial.printf("[WiFi][ERROR] " fmt "\n", ##__VA_ARGS__)
#else
  #define WIFI_LOG_I(...)
  #define WIFI_LOG_W(...)
  #define WIFI_LOG_E(...)
#endif

WifiState ESP32WifiManager::currentState = WIFI_STATE_DISCONNECTED;
std::vector<WifiScanItem> ESP32WifiManager::scanList;
SemaphoreHandle_t ESP32WifiManager::wifiMutex = NULL;
unsigned long ESP32WifiManager::lastConnectAttempt = 0;
unsigned long ESP32WifiManager::lastCheckTime = 0;
bool ESP32WifiManager::isConnecting = false;
bool ESP32WifiManager::isScanRunning = false;
bool ESP32WifiManager::autoReconnectEnabled = true;
uint32_t ESP32WifiManager::autoReconnectIntervalMs = 15000;

ESP32WifiManager::GetSavedCredentialsCallback ESP32WifiManager::getCredentialsCb = nullptr;
ESP32WifiManager::OnCredentialsSavedCallback ESP32WifiManager::saveCredentialsCb = nullptr;
ESP32WifiManager::OnStateChangedCallback ESP32WifiManager::stateChangedCb = nullptr;

struct ConnectParams {
    String ssid;
    String pass;
};

void ESP32WifiManager::setState(WifiState newState) {
    if (currentState != newState) {
        currentState = newState;
        if (stateChangedCb) {
            stateChangedCb(currentState);
        }
    }
}

void ESP32WifiManager::setCredentialsCallbacks(GetSavedCredentialsCallback getCb, OnCredentialsSavedCallback saveCb) {
    getCredentialsCb = getCb;
    saveCredentialsCb = saveCb;
}

void ESP32WifiManager::setOnStateChanged(OnStateChangedCallback cb) {
    stateChangedCb = cb;
}

void ESP32WifiManager::setAutoReconnect(bool enable, uint32_t intervalMs) {
    autoReconnectEnabled = enable;
    autoReconnectIntervalMs = intervalMs;
}

void ESP32WifiManager::init(const String& defaultSsid, const String& defaultPass) {
    if (wifiMutex == NULL) {
        wifiMutex = xSemaphoreCreateMutex();
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);

    String targetSSID = defaultSsid;
    String targetPass = defaultPass;

    // Kiem tra credentials tu callback hoac Preferences NVS
    if (targetSSID.isEmpty()) {
        if (getCredentialsCb) {
            getCredentialsCb(targetSSID, targetPass);
        } else {
            Preferences prefs;
            if (prefs.begin("esp32_wifi", true)) {
                targetSSID = prefs.getString("ssid", "");
                targetPass = prefs.getString("pass", "");
                prefs.end();
            }
        }
    }

    if (!targetSSID.isEmpty()) {
        WIFI_LOG_I("Found saved credentials for SSID '%s', connecting...", targetSSID.c_str());
        connect(targetSSID, targetPass);
    }
}

void ESP32WifiManager::scanTask(void* param) {
    {
        WIFI_LOG_I("Starting fast async scan...");
        // Active scan, khong quet mang an, 80ms moi kenh -> Quet sieu nhanh ~1s
        int n = WiFi.scanNetworks(false, false, false, 80);

        std::vector<WifiScanItem> tempResults;
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                String s = WiFi.SSID(i);
                if (s.length() > 0) {
                    bool found = false;
                    for (auto& item : tempResults) {
                        if (item.ssid == s) {
                            found = true;
                            if (WiFi.RSSI(i) > item.rssi) {
                                item.rssi = WiFi.RSSI(i);
                                item.channel = WiFi.channel(i);
                            }
                            break;
                        }
                    }
                    if (!found) {
                        WifiScanItem item;
                        item.ssid = s;
                        item.rssi = WiFi.RSSI(i);
                        item.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                        item.channel = WiFi.channel(i);
                        tempResults.push_back(item);
                    }
                }
            }
            // Sap xep mang song khoe nhat len dau
            std::sort(tempResults.begin(), tempResults.end(), [](const WifiScanItem& a, const WifiScanItem& b) {
                return a.rssi > b.rssi;
            });
        }
        WiFi.scanDelete();

        if (wifiMutex != NULL && xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            scanList = tempResults;
            isScanRunning = false;
            if (currentState == WIFI_STATE_SCANNING) {
                setState((WiFi.status() == WL_CONNECTED) ? WIFI_STATE_CONNECTED : WIFI_STATE_DISCONNECTED);
            }
            xSemaphoreGive(wifiMutex);
        }

        WIFI_LOG_I("Fast scan finished, found %d unique networks.", (int)tempResults.size());
    }

    vTaskDelete(NULL);
}

void ESP32WifiManager::startScan() {
    if (isScanRunning) return;
    isScanRunning = true;
    setState(WIFI_STATE_SCANNING);

    BaseType_t res = xTaskCreatePinnedToCore(
        scanTask,
        "WifiScanTask",
        4 * 1024,
        NULL,
        1,
        NULL,
        0 // Run on Core 0
    );

    if (res != pdPASS) {
        isScanRunning = false;
        setState(WIFI_STATE_DISCONNECTED);
    }
}

bool ESP32WifiManager::isScanning() {
    return isScanRunning;
}

std::vector<WifiScanItem> ESP32WifiManager::getScanResults() {
    std::vector<WifiScanItem> copy;
    if (wifiMutex != NULL && xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = scanList;
        xSemaphoreGive(wifiMutex);
    }
    return copy;
}

void ESP32WifiManager::connectTask(void* param) {
    {
        ConnectParams* p = (ConnectParams*)param;
        String ssid = p->ssid;
        String pass = p->pass;
        delete p;

        WIFI_LOG_I("Connecting to: %s", ssid.c_str());

        WiFi.disconnect(true);
        vTaskDelay(pdMS_TO_TICKS(200));
        WiFi.begin(ssid.c_str(), pass.c_str());

        unsigned long startMs = millis();
        bool success = false;

        while (millis() - startMs < 12000) { // Timeout 12s
            if (WiFi.status() == WL_CONNECTED) {
                success = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        if (wifiMutex != NULL && xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (success) {
                setState(WIFI_STATE_CONNECTED);
                WIFI_LOG_I("Connected! IP: %s", WiFi.localIP().toString().c_str());

                // Luu thong tin WiFi
                if (saveCredentialsCb) {
                    saveCredentialsCb(ssid, pass);
                } else {
                    Preferences prefs;
                    if (prefs.begin("esp32_wifi", false)) {
                        prefs.putString("ssid", ssid);
                        prefs.putString("pass", pass);
                        prefs.end();
                    }
                }
            } else {
                setState(WIFI_STATE_CONNECT_FAILED);
                WIFI_LOG_W("Connection failed!");
            }
            isConnecting = false;
            xSemaphoreGive(wifiMutex);
        }
    }

    vTaskDelete(NULL);
}

void ESP32WifiManager::connect(const String& ssid, const String& password) {
    if (isConnecting) return;
    isConnecting = true;
    setState(WIFI_STATE_CONNECTING);
    lastConnectAttempt = millis();

    ConnectParams* params = new ConnectParams{ssid, password};

    BaseType_t res = xTaskCreatePinnedToCore(
        connectTask,
        "WifiConnectTask",
        4 * 1024,
        params,
        1,
        NULL,
        0 // Run on Core 0
    );

    if (res != pdPASS) {
        delete params;
        isConnecting = false;
        setState(WIFI_STATE_CONNECT_FAILED);
    }
}

void ESP32WifiManager::disconnect() {
    WiFi.disconnect(true);
    setState(WIFI_STATE_DISCONNECTED);
}

bool ESP32WifiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

WifiState ESP32WifiManager::getState() {
    if (WiFi.status() == WL_CONNECTED) {
        return WIFI_STATE_CONNECTED;
    }
    return currentState;
}

const char* ESP32WifiManager::getStateString() {
    switch (getState()) {
        case WIFI_STATE_CONNECTED: return "Đã kết nối";
        case WIFI_STATE_CONNECTING: return "Đang kết nối...";
        case WIFI_STATE_SCANNING: return "Đang quét mạng...";
        case WIFI_STATE_CONNECT_FAILED: return "Kết nối thất bại";
        default: return "Chưa kết nối";
    }
}

String ESP32WifiManager::getConnectedSSID() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.SSID();
    }
    return "";
}

String ESP32WifiManager::getIPAddress() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

String ESP32WifiManager::getMacAddress() {
    return WiFi.macAddress();
}

int32_t ESP32WifiManager::getRSSI() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return -100;
}

int ESP32WifiManager::getSignalPercent() {
    if (WiFi.status() != WL_CONNECTED) return 0;
    int rssi = WiFi.RSSI();
    if (rssi <= -100) return 0;
    if (rssi >= -50) return 100;
    return 2 * (rssi + 100);
}

void ESP32WifiManager::update() {
    unsigned long now = millis();
    if (now - lastCheckTime >= 5000) { // Moi 5s kiem tra trang thai
        lastCheckTime = now;
        if (WiFi.status() == WL_CONNECTED) {
            setState(WIFI_STATE_CONNECTED);
        } else if (autoReconnectEnabled && !isConnecting && !isScanRunning) {
            // Tu dong ket noi lai dinh ky (Rule 5 - GEMINI.md)
            if (now - lastConnectAttempt >= autoReconnectIntervalMs) {
                String savedSSID = "";
                String savedPass = "";
                bool hasCreds = false;

                if (getCredentialsCb) {
                    hasCreds = getCredentialsCb(savedSSID, savedPass) && !savedSSID.isEmpty();
                } else {
                    Preferences prefs;
                    if (prefs.begin("esp32_wifi", true)) {
                        savedSSID = prefs.getString("ssid", "");
                        savedPass = prefs.getString("pass", "");
                        prefs.end();
                        hasCreds = !savedSSID.isEmpty();
                    }
                }

                if (hasCreds) {
                    connect(savedSSID, savedPass);
                }
            }
        }
    }
}
