#define BLYNK_TEMPLATE_ID "TMPL6-UvallYr"
#define BLYNK_TEMPLATE_NAME "SmartDoorLock"
#define BLYNK_AUTH "FSa9wepfkHx1hpcv-F1FND1p9Mq9FDb9"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266HTTPClient.h>

// Cấu hình WiFi
#define WIFI_SSID "Sontan02"
#define WIFI_PASS "0914381080"

// Cấu hình chân
#define SIGNAL_PIN D5  // Gửi tín hiệu đến Arduino qua D5

// URL của Google Script
const String scriptURL = "https://script.google.com/macros/s/AKfycbygLGYZuBsBDQQxQSxfjj1iFQPdXNyPtUO7RofS9jElsmehAakarjRK0SNklSYX-Gfi/exec";

// Biến trạng thái
bool serialDebug = true;
unsigned long lastConnectCheck = 0;
const unsigned long checkInterval = 30000; // 30 giây

void setup() {
    Serial.begin(9600);
    delay(1000);
    
    pinMode(SIGNAL_PIN, OUTPUT);
    digitalWrite(SIGNAL_PIN, LOW);
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    debugPrint("Đang kết nối WiFi");
    
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
        delay(500);
        debugPrint(".");
        wifiAttempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        debugPrint("\nWiFi đã kết nối!");
        debugPrint("IP: " + WiFi.localIP().toString());
        Blynk.begin(BLYNK_AUTH, WIFI_SSID, WIFI_PASS);
        debugPrint("Đã kết nối Blynk");
    } else {
        debugPrint("\nKhông thể kết nối WiFi!");
    }
    
    sendToGoogleSheet("System", "ESP_Started");
}

void debugPrint(String message) {
    if (serialDebug) Serial.println(message);
}

// Nút mở cửa (V1)
BLYNK_WRITE(V1) {  
    int value = param.asInt();
    if (value == 1) {
        debugPrint("BLYNK mở cửa!");
        digitalWrite(SIGNAL_PIN, HIGH);
        delay(500); // HIGH 500ms cho mở cửa
        digitalWrite(SIGNAL_PIN, LOW);
        sendToGoogleSheet("Blynk", "Command_Sent");
    }
}

// Nút Admin Menu (V2)
BLYNK_WRITE(V2) {  
    int value = param.asInt();
    if (value == 1) {
        debugPrint("BLYNK yêu cầu Admin Menu!");
        digitalWrite(SIGNAL_PIN, HIGH);
        delay(1000); // HIGH 1000ms cho Admin Menu
        digitalWrite(SIGNAL_PIN, LOW);
        sendToGoogleSheet("Admin", "Menu_Requested");
    }
}

// Nút Disable (V3)
BLYNK_WRITE(V3) {
    int value = param.asInt();
    if (value == 1) {
        debugPrint("BLYNK khóa hệ thống!");
        digitalWrite(SIGNAL_PIN, HIGH);
        delay(1500); // HIGH 1500ms cho khóa hệ thống
        digitalWrite(SIGNAL_PIN, LOW);
        sendToGoogleSheet("System", "Locked");
    } else {
        debugPrint("BLYNK mở khóa hệ thống!");
        digitalWrite(SIGNAL_PIN, HIGH);
        delay(2000); // HIGH 2000ms cho mở khóa hệ thống
        digitalWrite(SIGNAL_PIN, LOW);
        sendToGoogleSheet("System", "Unlocked");
    }
}

void loop() {
    Blynk.run();
    
    unsigned long currentTime = millis();
    if (currentTime - lastConnectCheck > checkInterval) {
        checkConnection();
        lastConnectCheck = currentTime;
    }
    
    if (Serial.available() > 0) {
        String data = Serial.readStringUntil('\n');
        data.trim();
        debugPrint("Received from Arduino: " + data);
        if (data.startsWith("LOG:")) {
            data = data.substring(4);
            int commaIndex = data.indexOf(',');
            if (commaIndex > 0) {
                String method = data.substring(0, commaIndex);
                String status = data.substring(commaIndex + 1);
                debugPrint("Parsed: Method=" + method + ", Status=" + status);
                sendToGoogleSheet(method, status);
                
                // Gửi thông báo trộm qua Blynk nếu phát hiện trộm
                if (method == "Theft" && status == "Detected") {
                    Blynk.logEvent("theft_detected", "CẢNH BÁO: Phát hiện trộm!");
                    debugPrint("Đã gửi thông báo trộm qua Blynk");
                }
            } else {
                debugPrint("Invalid log format");
            }
        }
    }
}

void checkConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        debugPrint("WiFi đã mất kết nối. Đang kết nối lại...");
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            debugPrint("WiFi đã kết nối lại thành công");
            Blynk.connect();
        } else {
            debugPrint("Không thể kết nối lại WiFi");
        }
    } else if (!Blynk.connected()) {
        debugPrint("Blynk đã mất kết nối. Đang kết nối lại...");
        Blynk.connect();
    }
}

void sendToGoogleSheet(String method, String status) {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        HTTPClient http;
        client.setInsecure();
        String url = scriptURL + "?method=" + method + "&status=" + status;
        debugPrint("URL: " + url);
        
        http.begin(client, url);
        int attempts = 0;
        int httpResponseCode = -1;
        
        while (httpResponseCode < 0 && attempts < 3) {
            httpResponseCode = http.GET();
            attempts++;
            if (httpResponseCode > 0) {
                String response = http.getString();
                debugPrint("HTTP Response code: " + String(httpResponseCode));
                debugPrint("Response: " + response);
            } else {
                debugPrint("Error code: " + String(httpResponseCode) + ". Thử lại lần " + String(attempts));
                delay(1000);
            }
        }
        http.end();
    } else {
        debugPrint("WiFi không kết nối, không thể gửi dữ liệu!");
    }
}