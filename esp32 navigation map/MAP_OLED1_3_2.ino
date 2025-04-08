#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDRESS 0x3C

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define SERVICE_UUID        "DD3F0AD1-6239-4E1F-81F1-91F6C9F01D86"
#define CHAR_INDICATE_UUID  "DD3F0AD2-6239-4E1F-81F1-91F6C9F01D86"
#define CHAR_WRITE_UUID     "DD3F0AD3-6239-4E1F-81F1-91F6C9F01D86"

BLECharacteristic *pWriteCharacteristic;
bool deviceConnected = false;
uint32_t initialDistance = 0;
uint32_t currentDistance = 0;

// ====== Các hàm vẽ ======
void drawLeftArrow() {
  display.fillRect(58, 5, 11, 25, SH110X_WHITE);
  display.fillRect(45, 5, 13, 10, SH110X_WHITE);
  display.fillTriangle(32, 10, 45, 0, 45, 20, SH110X_WHITE);
}

void drawRightArrow() {
  display.fillRect(58, 5, 11, 25, SH110X_WHITE);
  display.fillRect(69, 5, 13, 10, SH110X_WHITE);
  display.fillTriangle(95, 10, 82, 0, 82, 20, SH110X_WHITE);
}

void drawStraightArrow() {
  display.fillRect(58, 10, 11, 19, SH110X_WHITE);
  display.fillTriangle(63, 2, 47, 14, 79, 14, SH110X_WHITE);
}

void drawDestinationIcon() {
  display.fillRect(50, 10, 8, 30, SH110X_WHITE);
  display.fillRect(70, 10, 8, 30, SH110X_WHITE);
  display.fillRect(58, 22, 12, 6, SH110X_WHITE);
}

void drawDistanceBar(uint32_t distanceTravelled, uint32_t maxDistance) {
  const int barX = 10;
  const int barY = SCREEN_HEIGHT - 12;
  const int barWidth = 108;
  const int barHeight = 12;

  display.drawRect(barX, barY, barWidth, barHeight, SH110X_WHITE);
  int fillWidth = map(distanceTravelled, 0, maxDistance, 0, barWidth - 2);
  if (fillWidth < 0) fillWidth = 0;
  display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SH110X_WHITE);

  uint32_t remainingDistance = maxDistance - distanceTravelled;
  display.setTextSize(2);
  display.setCursor(SCREEN_WIDTH - 40, barY - 16);
  display.print(remainingDistance);
  display.print("m");
}

// ====== Các hàm gọi lại BLE ======
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Thiết bị đã kết nối");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(20, 20);
    display.println("Đã kết nối");
    display.display();
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("Thiết bị đã ngắt kết nối");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(20, 20);
    display.println("Đã ngắt kết nối");
    display.display();
    BLEDevice::startAdvertising();
  }
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue(); // Đúng: std::string từ getValue()
    Serial.print("Nhận được dữ liệu (bytes): ");
    Serial.println(value.length());

    if (value.length() < 1) return;

    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);

    if (value[0] == 0x01 && value.length() >= 3) {
      uint8_t speed = value[1];
      display.setTextSize(2);
      display.setCursor(10, 10);
      display.print(speed);
      display.print(" km/h");

      uint8_t direction = value[2];
      switch (direction) {
        case 0x08: drawLeftArrow(); break;   // Mũi tên trái
        case 0x0A: drawRightArrow(); break;  // Mũi tên phải
        case 0x04: drawStraightArrow(); break; // Mũi tên thẳng
        default: break;
      }

      if (value.length() > 3) {
        std::string distanceStr = value.substr(3);
        if (distanceStr == "No route") {
          drawDestinationIcon(); // Vẽ biểu tượng đích đến
          Serial.println("Không có tuyến đường");
        } else {
          float distance = 0;
          if (distanceStr.find("km") != std::string::npos) {
            distanceStr.erase(distanceStr.find("km"), 2);
            distance = atof(distanceStr.c_str()) * 1000;
          } else {
            distance = atoi(distanceStr.c_str());
          }

          currentDistance = static_cast<uint32_t>(distance);
          if (initialDistance == 0 || currentDistance > initialDistance) {
            initialDistance = currentDistance;
          }

          drawDistanceBar(initialDistance - currentDistance, initialDistance);
        }
      }
    }

    display.display();
  }
};

// ====== Cài đặt & Vòng lặp ======
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // Điều chỉnh dựa trên cách nối dây của bạn
  display.begin(OLED_I2C_ADDRESS, true);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(20, 20);
  display.println("Đang khởi động...");
  display.display();

  BLEDevice::init("ESP32_Sygic_HUD");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pIndicateCharacteristic = pService->createCharacteristic(
    CHAR_INDICATE_UUID, BLECharacteristic::PROPERTY_INDICATE);
  pIndicateCharacteristic->addDescriptor(new BLE2902());

  pWriteCharacteristic = pService->createCharacteristic(
    CHAR_WRITE_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWriteCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();

  Serial.println("Đang chờ kết nối từ Sygic...");
}

void loop() {
  delay(100);
}
