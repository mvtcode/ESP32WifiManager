#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <functional>

enum WifiState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_CONNECT_FAILED
};

struct WifiScanItem {
    String ssid;
    int32_t rssi;
    bool isEncrypted;
    uint8_t channel;
};

class ESP32WifiManager {
public:
    typedef std::function<bool(String& ssid, String& password)> GetSavedCredentialsCallback;
    typedef std::function<void(const String& ssid, const String& password)> OnCredentialsSavedCallback;
    typedef std::function<void(WifiState state)> OnStateChangedCallback;

    static void init(const String& defaultSsid = "", const String& defaultPass = "");
    static void update();

    // Async Scan (Non-blocking FreeRTOS Task)
    static void startScan();
    static bool isScanning();
    static std::vector<WifiScanItem> getScanResults();

    // Async Connect (Non-blocking FreeRTOS Task)
    static void connect(const String& ssid, const String& password);
    static void disconnect();
    static bool isConnected();
    static WifiState getState();
    static const char* getStateString();

    // Telemetry getters
    static String getConnectedSSID();
    static String getIPAddress();
    static String getMacAddress();
    static int32_t getRSSI();
    static int getSignalPercent(); // 0 - 100%

    // Callbacks & Custom Storage Configuration
    static void setCredentialsCallbacks(GetSavedCredentialsCallback getCb, OnCredentialsSavedCallback saveCb);
    static void setOnStateChanged(OnStateChangedCallback cb);
    static void setAutoReconnect(bool enable, uint32_t intervalMs = 15000);

private:
    static WifiState currentState;
    static std::vector<WifiScanItem> scanList;
    static SemaphoreHandle_t wifiMutex;
    static unsigned long lastConnectAttempt;
    static unsigned long lastCheckTime;
    static bool isConnecting;
    static bool isScanRunning;
    static bool autoReconnectEnabled;
    static uint32_t autoReconnectIntervalMs;

    static GetSavedCredentialsCallback getCredentialsCb;
    static OnCredentialsSavedCallback saveCredentialsCb;
    static OnStateChangedCallback stateChangedCb;

    static void scanTask(void* param);
    static void connectTask(void* param);
    static void setState(WifiState newState);
};

// Alias tuong thich 100% voi code cu
using WifiService = ESP32WifiManager;
