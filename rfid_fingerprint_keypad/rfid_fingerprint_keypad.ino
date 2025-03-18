#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_Fingerprint.h>
#include <Wire.h>
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SoftwareSerial.h>

// Pin definitions
#define SS_PIN 10
#define RST_PIN 9
#define I2C_ADDR_KEYPAD 0x20
#define I2C_ADDR_LCD 0x27
#define SERVO_PIN 8
#define SIGNAL_PIN 5  // Nhận tín hiệu từ ESP8266 qua D5
#define BUZZER_PIN 4
#define VIBRATION_PIN 2  // Cảm biến rung trên D2

// Các đối tượng
Servo doorServo;
LiquidCrystal_I2C lcd(I2C_ADDR_LCD, 16, 2);
SoftwareSerial fingerSerial(6, 7);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);
#define espSerial Serial

// Keypad configuration
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {0, 1, 2, 3};
byte colPins[COLS] = {4, 5, 6, 7};
Keypad_I2C keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS, I2C_ADDR_KEYPAD);

// RFID configuration
MFRC522 mfrc522(SS_PIN, RST_PIN);
byte authorizedUID[] = {0x5B, 0xC5, 0x38, 0x02};

// Biến trạng thái
char mode = 'D';
int failCount = 0;
bool connectionTested = false;
unsigned long lastVibrationCheck = 0;
const unsigned long vibrationInterval = 1000;
bool adminMode = false; // Trạng thái Admin Menu
String currentPIN = "1234"; // PIN hiện tại, có thể thay đổi

void setup() {
    espSerial.begin(9600);
    delay(1000);
    
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(SIGNAL_PIN, INPUT);
    pinMode(VIBRATION_PIN, INPUT);
    
    Wire.begin();
    keypad.begin(makeKeymap(keys));
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Khoi dong...");
    delay(1000);
    
    fingerSerial.begin(57600);
    delay(100);
    lcd.clear();
    lcd.print("Khoi dong FP...");
    lcd.print(finger.verifyPassword() ? " OK!" : " ERROR!");
    delay(1000);
    
    SPI.begin();
    mfrc522.PCD_Init();
    
    doorServo.attach(SERVO_PIN);
    doorServo.write(0);
    
    testESPConnection();
    showMenu();
}

void testESPConnection() {
    lcd.clear();
    lcd.print("Kiem tra ESP...");
    sendLog("SYSTEM", "STARTUP");
    lcd.setCursor(0, 1);
    lcd.print("ESP OK!");
    delay(1000);
    connectionTested = true;
}

void beepShort() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
}

void beepLong() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(500);
    digitalWrite(BUZZER_PIN, LOW);
}

void beepWarning() {
    for (int i = 0; i < 3; i++) {
        beepShort();
        delay(100);
    }
}

void beepError() {
    for (int i = 0; i < 5; i++) {
        beepShort();
        delay(50);
    }
}

void beepTheft() {
    for (int i = 0; i < 10; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
        delay(200);
    }
}

void sendLog(String method, String status) {
    String logMsg = "LOG:" + method + "," + status;
    espSerial.println(logMsg);
    delay(200);
}

void loop() {
    // Kiểm tra tín hiệu từ ESP8266
    if (digitalRead(SIGNAL_PIN) == HIGH) {
        unsigned long signalStart = millis();
        while (digitalRead(SIGNAL_PIN) == HIGH) {
            if (millis() - signalStart > 1500) break; // Thoát nếu tín hiệu quá dài
        }
        unsigned long signalDuration = millis() - signalStart;
        
        if (signalDuration >= 800 && signalDuration <= 1200) { // 1000ms ± 200ms cho Admin Menu
            adminMode = true;
            showAdminMenu();
        } else if (signalDuration >= 300 && signalDuration <= 700) { // 500ms ± 200ms cho mở cửa
            grantAccess("Blynk");
        }
        delay(500); // Chờ tín hiệu kết thúc
    }

    char key = keypad.getKey();
    if (key) {
        if (adminMode) handleAdminKeyPress(key);
        else handleKeyPress(key);
    }

    if (!adminMode) {
        if (mode == 'B') checkFingerprint();
        else if (mode == 'C') checkRFID();

        unsigned long currentTime = millis();
        if (currentTime - lastVibrationCheck >= vibrationInterval) {
            checkVibration();
            lastVibrationCheck = currentTime;
        }
    }
}

void showMenu() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("A: PIN  B: FP");
    lcd.setCursor(0, 1);
    lcd.print("C: RFID D: Menu");
    failCount = 0;
}

void showAdminMenu() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("A: Doi PIN");
    lcd.setCursor(0, 1);
    lcd.print("B: DK VT  D: Exit");
}

void handleKeyPress(char key) {
    beepShort();
    if (key == 'A' || key == 'B' || key == 'C') {
        mode = key;
        failCount = 0;
        lcd.clear();
        lcd.setCursor(0, 0);
        if (key == 'A') lcd.print("Nhap ma PIN:");
        else if (key == 'B') lcd.print("Quet van tay...");
        else if (key == 'C') lcd.print("Quet the RFID...");
    } else if (key == 'D') {
        mode = 'D';
        showMenu();
    } else if (mode == 'A') {
        checkPIN(key);
    }
}

void handleAdminKeyPress(char key) {
    beepShort();
    if (key == 'A') {
        changePIN();
    } else if (key == 'B') {
        enrollFingerprint();
    } else if (key == 'D') {
        adminMode = false;
        showMenu();
    }
}

void changePIN() {
    lcd.clear();
    lcd.print("Nhap PIN moi:");
    String newPIN = "";
    
    while (true) {
        char key = keypad.getKey();
        if (key) {
            beepShort();
            if (key >= '0' && key <= '9') {
                newPIN += key;
                lcd.setCursor(newPIN.length() - 1, 1);
                lcd.print("*");
            } else if (key == '#') {
                if (newPIN.length() >= 4) { // Đảm bảo PIN mới ít nhất 4 số
                    currentPIN = newPIN;
                    lcd.clear();
                    lcd.print("PIN da doi!");
                    sendLog("Admin", "PIN_Changed");
                    delay(2000);
                    showAdminMenu();
                    break;
                } else {
                    lcd.clear();
                    lcd.print("PIN qua ngan!");
                    beepWarning();
                    delay(1000);
                    lcd.clear();
                    lcd.print("Nhap PIN moi:");
                }
            }
        }
    }
}

void enrollFingerprint() {
    lcd.clear();
    lcd.print("Dat van tay...");
    int id = getNextFingerprintID();
    if (id == -1) {
        lcd.clear();
        lcd.print("Het ID!");
        beepError();
        delay(2000);
        showAdminMenu();
        return;
    }

    int p = -1;
    while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        switch (p) {
            case FINGERPRINT_OK:
                lcd.setCursor(0, 1);
                lcd.print("Da quet!");
                break;
            case FINGERPRINT_NOFINGER:
                break;
            default:
                lcd.clear();
                lcd.print("Loi quet!");
                beepError();
                delay(2000);
                showAdminMenu();
                return;
        }
    }

    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) {
        lcd.clear();
        lcd.print("Loi xu ly!");
        beepError();
        delay(2000);
        showAdminMenu();
        return;
    }

    lcd.clear();
    lcd.print("Dat lai lan 2...");
    delay(2000);
    p = -1;
    while (p != FINGERPRINT_OK) {
        p = finger.getImage();
    }
    
    p = finger.image2Tz(2);
    if (p != FINGERPRINT_OK) {
        lcd.clear();
        lcd.print("Loi xu ly!");
        beepError();
        delay(2000);
        showAdminMenu();
        return;
    }

    p = finger.createModel();
    if (p == FINGERPRINT_OK) {
        p = finger.storeModel(id);
        if (p == FINGERPRINT_OK) {
            lcd.clear();
            lcd.print("DK thanh cong!");
            sendLog("Admin", "Fingerprint_Added_ID" + String(id));
            beepLong();
            delay(2000);
            showAdminMenu();
        } else {
            lcd.clear();
            lcd.print("Loi luu!");
            beepError();
            delay(2000);
            showAdminMenu();
        }
    } else {
        lcd.clear();
        lcd.print("Khong khop!");
        beepError();
        delay(2000);
        showAdminMenu();
    }
}

int getNextFingerprintID() {
    for (int id = 1; id < 128; id++) { // Giới hạn 127 vân tay
        if (finger.loadModel(id) != FINGERPRINT_OK) {
            return id; // ID này chưa được dùng
        }
    }
    return -1; // Không còn ID trống
}

void checkPIN(char key) {
    static String enteredPIN = "";
    
    if (key == '#') {
        if (enteredPIN == currentPIN) {
            lcd.clear();
            lcd.print("PIN OK! Open door");
            beepLong();
            grantAccess("PIN");
        } else {
            failCount++;
            lcd.clear();
            lcd.print("PIN Sai!");
            beepWarning();
            sendLog("PIN", "Failed");
            delay(1000);
            if (failCount >= 5) askToExit();
            else {
                lcd.clear();
                lcd.print("Nhap ma PIN:");
            }
        }
        enteredPIN = "";
    } else if (key >= '0' && key <= '9') {
        enteredPIN += key;
        lcd.setCursor(enteredPIN.length() - 1, 1);
        lcd.print("*");
    }
}

void checkRFID() {
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;
    
    lcd.clear();
    lcd.print("Quet RFID...");
    
    if (checkAccess()) {
        grantAccess("RFID");
    } else {
        failCount++;
        lcd.setCursor(0, 1);
        lcd.print("RFID Sai!");
        beepWarning();
        sendLog("RFID", "Failed");
        delay(1000);
        if (failCount >= 5) askToExit();
        else {
            lcd.clear();
            lcd.print("Quet the RFID...");
        }
    }
    mfrc522.PICC_HaltA();
}

void checkFingerprint() {
    int p = finger.getImage();
    if (p != FINGERPRINT_OK) return;
    
    p = finger.image2Tz();
    if (p != FINGERPRINT_OK) return;
    
    p = finger.fingerFastSearch();
    if (p == FINGERPRINT_OK) {
        lcd.clear();
        lcd.print("FP OK! Open door");
        grantAccess("Fingerprint");
    } else {
        failCount++;
        lcd.clear();
        lcd.print("FP Sai!");
        beepWarning();
        sendLog("Fingerprint", "Failed");
        delay(1000);
        if (failCount >= 5) askToExit();
        else {
            lcd.clear();
            lcd.print("Quet van tay...");
        }
    }
}

void grantAccess(String method) {
    beepLong();
    sendLog(method, "Success");
    lcd.clear();
    lcd.print("Cua Mo...");
    doorServo.write(90);
    delay(2000);
    doorServo.write(0);
    lcd.clear();
    lcd.print("Cua Dong!");
    delay(1000);
    mode = 'D';
    showMenu();
}

bool checkAccess() {
    return (memcmp(mfrc522.uid.uidByte, authorizedUID, sizeof(authorizedUID)) == 0);
}

void askToExit() {
    lcd.clear();
    lcd.print("Sai 5 lan! Nhap D");
    beepError();
    while (true) {
        char key = keypad.getKey();
        if (key == 'D') {
            mode = 'D';
            showMenu();
            break;
        }
    }
}

void checkVibration() {
    if (digitalRead(VIBRATION_PIN) == HIGH) {
        lcd.clear();
        lcd.print("CANH BAO TROM!");
        beepTheft();
        sendLog("Theft", "Detected");
        delay(2000);
        showMenu();
    }
}