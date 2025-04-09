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

// ====== Drawing Functions ======
void drawLeftArrow() {
  display.fillRect(85, 5, 11, 25, SH110X_WHITE);
  display.fillRect(72, 5, 13, 10, SH110X_WHITE);
  display.fillTriangle(59, 10, 72, 0, 72, 20, SH110X_WHITE);
}

void drawRightArrow() {
  display.fillRect(85, 5, 11, 25, SH110X_WHITE);
  display.fillRect(96, 5, 13, 10, SH110X_WHITE);
  display.fillTriangle(122, 10, 109, 0, 109, 20, SH110X_WHITE);
}

void drawStraightArrow() {
  display.fillRect(90, 10, 11, 19, SH110X_WHITE);
  display.fillTriangle(95, 2, 79, 14, 111, 14, SH110X_WHITE);
}

void drawDestinationIcon() {
  display.fillRect(80, 10, 8, 30, SH110X_WHITE);
  display.fillRect(100, 10, 8, 30, SH110X_WHITE);
  display.fillRect(88, 22, 12, 6, SH110X_WHITE);
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
  display.setTextSize(1);
  display.setCursor(SCREEN_WIDTH - 30, barY - 10);
  display.print(remainingDistance);
  display.print("m");
}

// ====== BLE Callbacks ======
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Device connected");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(20, 20);
    display.println("Connected");
    display.display();
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("Device disconnected");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(20, 20);
    display.println("Disconnected");
    display.display();
    BLEDevice::startAdvertising();
  }
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    Serial.print("Received data (bytes): ");
    Serial.println(value.length());

    if (value.length() < 1) return;

    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);

    if (value[0] == 0x01 && value.length() >= 3) {
      uint8_t speed = value[1];
      display.setTextSize(2);
      display.setCursor(5, 10);
      display.print(speed);

      display.setTextSize(1);
      display.setCursor(5, 30);
      display.print("km/h");

      uint8_t direction = value[2];
      switch (direction) {
        case 0x08: drawLeftArrow(); break;
        case 0x0A: drawRightArrow(); break;
        case 0x04: drawStraightArrow(); break;
        default: break;
      }

      if (value.length() > 3) {
        std::string distanceStr = value.substr(3);
        if (distanceStr == "No route") {
          drawDestinationIcon();
          Serial.println("No route");
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

// ====== Setup & Loop ======
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  display.begin(OLED_I2C_ADDRESS, true);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(20, 20);
  display.println("Starting...");
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

  Serial.println("Waiting for connection from Sygic...");
}

void loop() {
  delay(100);
}
