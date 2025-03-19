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
bool adminMode = false;
String currentPIN = "1234";
bool systemLocked = false; // Trạng thái khóa hệ thống
unsigned long previousBuzzTime = 0; // Thời gian bật/tắt buzzer trước đó
bool buzzerState = false; // Trạng thái hiện tại của buzzer (HIGH/LOW)
bool theftDetected = false; // Trạng thái phát hiện trộm

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
    unsigned long startTime = millis();
    const unsigned long theftDuration = 10000; // 10 giây
    const unsigned long buzzInterval = 200; // Thời gian bật/tắt buzzer (200ms)
    espSerial.println("Buzzer Theft Started");
    
    while (millis() - startTime < theftDuration) {
        unsigned long currentTime = millis();
        if (currentTime - previousBuzzTime >= buzzInterval) {
            buzzerState = !buzzerState; // Đổi trạng thái buzzer
            digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
            previousBuzzTime = currentTime;
        }
    }
    digitalWrite(BUZZER_PIN, LOW); // Đảm bảo buzzer tắt khi thoát
    theftDetected = false; // Reset trạng thái trộm sau khi kết thúc
    espSerial.println("Buzzer Theft Ended");
}

void sendLog(String method, String status) {
    String logMsg = "LOG:" + method + "," + status;
    espSerial.println(logMsg);
    delay(200);
}

void loop() {
    if (digitalRead(SIGNAL_PIN) == HIGH) {
        unsigned long signalStart = millis();
        while (digitalRead(SIGNAL_PIN) == HIGH) {
            if (millis() - signalStart > 3000) break;
        }
        unsigned long signalDuration = millis() - signalStart;
        
        if (signalDuration >= 300 && signalDuration <= 700) {
            if (!systemLocked) grantAccess("Blynk");
        } else if (signalDuration >= 800 && signalDuration <= 1200) {
            if (!systemLocked) {
                adminMode = true;
                showAdminMenu();
            }
        } else if (signalDuration >= 1300 && signalDuration <= 1700) {
            systemLocked = true;
            lcd.clear();
            lcd.print("LOCKED!");
            sendLog("System", "Locked");
        } else if (signalDuration >= 1800 && signalDuration <= 2200) {
            systemLocked = false;
            showMenu();
            sendLog("System", "Unlocked");
        }
        delay(500);
    }

    if (systemLocked) return;

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
    
    // Nhập PIN mới lần 1
    while (true) {
        char key = keypad.getKey();
        if (key) {
            beepShort();
            if (key >= '0' && key <= '9') {
                newPIN += key;
                lcd.setCursor(0, 1);
                lcd.print(newPIN); // Hiển thị trực tiếp PIN
            } else if (key == '#') {
                if (newPIN.length() >= 4) {
                    break; // Thoát vòng lặp để sang xác nhận
                } else {
                    lcd.clear();
                    lcd.print("PIN qua ngan!");
                    beepWarning();
                    delay(1000);
                    lcd.clear();
                    lcd.print("Nhap PIN moi:");
                }
            } else if (key == 'D') { // Thoát về Admin Menu
                showAdminMenu();
                return;
            } else if (key == '*') { // Xóa PIN đang nhập
                newPIN = "";
                lcd.setCursor(0, 1);
                lcd.print("                ");
                lcd.setCursor(0, 1);
            }
        }
    }

    // Xác nhận PIN mới
    lcd.clear();
    lcd.print("Xac nhan PIN:");
    String confirmPIN = "";
    int confirmFailCount = 0; // Đếm số lần xác nhận sai
    
    while (true) {
        char key = keypad.getKey();
        if (key) {
            beepShort();
            if (key >= '0' && key <= '9') {
                confirmPIN += key;
                lcd.setCursor(0, 1);
                lcd.print(confirmPIN); // Hiển thị trực tiếp PIN xác nhận
            } else if (key == '#') {
                if (confirmPIN == newPIN) {
                    currentPIN = newPIN;
                    lcd.clear();
                    lcd.print("PIN da doi!");
                    sendLog("Admin", "PIN_Changed");
                    delay(2000);
                    showAdminMenu();
                    break;
                } else {
                    confirmFailCount++;
                    if (confirmFailCount >= 5) {
                        lcd.clear();
                        lcd.print("Co muon ve menu?");
                        lcd.setCursor(0, 1);
                        lcd.print("D: Co");
                        beepError();
                        while (true) {
                            char exitKey = keypad.getKey();
                            if (exitKey == 'D') {
                                showAdminMenu();
                                return;
                            }
                        }
                    } else {
                        lcd.clear();
                        lcd.print("PIN khong khop!");
                        lcd.setCursor(0, 1);
                        lcd.print("Nhap lai:");
                        beepWarning();
                        delay(1000);
                        lcd.clear();
                        lcd.print("Xac nhan PIN:");
                        confirmPIN = ""; // Reset để nhập lại
                    }
                }
            } else if (key == 'D') { // Thoát về Admin Menu
                showAdminMenu();
                return;
            } else if (key == '*') { // Xóa PIN xác nhận đang nhập
                confirmPIN = "";
                lcd.setCursor(0, 1);
                lcd.print("                ");
                lcd.setCursor(0, 1);
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
        char key = keypad.getKey();
        if (key == 'D') {
            showAdminMenu();
            return;
        }
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
        char key = keypad.getKey();
        if (key == 'D') {
            showAdminMenu();
            return;
        }
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
    for (int id = 1; id < 128; id++) {
        if (finger.loadModel(id) != FINGERPRINT_OK) {
            return id;
        }
    }
    return -1;
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
    } else if (key == '*') {
        enteredPIN = "";
        lcd.setCursor(0, 1);
        lcd.print("                ");
        lcd.setCursor(0, 1);
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
    if (digitalRead(VIBRATION_PIN) == HIGH && !theftDetected) {
        theftDetected = true;
        lcd.clear();
        lcd.print("CANH BAO TROM!");
        sendLog("Theft", "Detected");
        beepTheft();
        delay(3000);
        showMenu();
    } else if (digitalRead(VIBRATION_PIN) == LOW) {
        theftDetected = false;
    }
}