# ESP32WifiManager

Thư viện quản lý kết nối WiFi bất đồng bộ (Non-blocking / Asynchronous) cho ESP32 chạy trên nền FreeRTOS:
- **Async Scan**: Quét mạng WiFi trên FreeRTOS task riêng (Core 0), không giật lag giao diện đồ họa / audio trên main loop.
- **Async Connect**: Kết nối WiFi bất đồng bộ với timeout 12s, an toàn với Mutex.
- **Auto Reconnect**: Tự động thử kết nối lại định kỳ khi mất mạng (mặc định 15s - tuân thủ quy chuẩn non-blocking).
- **Telemetry**: Đo cường độ sóng RSSI, tính % tín hiệu (0 - 100%), Uptime, IP Address, MAC Address.
- **Custom Storage / Callbacks**: Tự động lưu thông tin mạng vào NVS Preferences hoặc qua callback tùy biến.

---

## Cài đặt (Installation)

### PlatformIO
Thêm vào `platformio.ini`:
```ini
lib_deps =
    https://github.com/mvtcode/ESP32WifiManager.git
```

---

## Hướng dẫn sử dụng

```cpp
#include <ESP32WifiManager.h>

void setup() {
    Serial.begin(115200);

    // Khoi tao voi SSID / Pass mac dinh hoac tu NVS
    ESP32WifiManager::init("WiFi_Name", "WiFi_Password");

    // Quet mang bat dong bo
    ESP32WifiManager::startScan();
}

void loop() {
    // Goi ham update trong loop de duy tri auto-reconnect
    ESP32WifiManager::update();

    if (ESP32WifiManager::isConnected()) {
        // Da ket noi
    }
}
```

---

## License
MIT License.
