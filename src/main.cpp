#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <IRremote.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "html_content.h" // 包含生成的头文件
// 引脚定义
const int STBY = 17; // 使能引脚
const int PWMA = 12; // 电机A速度控制
const int AIN1 = 14; // 电机A方向控制
const int AIN2 = 13;
const int PWMB = 25; // 电机B速度控制
const int BIN1 = 26; // 电机B方向控制
const int BIN2 = 27;

const char *ssid = "OpenWrtV";      // 你的WiFi SSID
const char *password = "123456987"; // 你的WiFi密码
AsyncWebServer server(80);

// 定义红外接收引脚
const int RECV_PIN = 19;
// 创建一个红外接收对象
IRrecv irrecv(RECV_PIN);
decode_results results;
// BLE UUIDs
#define SERVICE_UUID_NAME "bridge"
#define CHARACTERISTIC_UUID_NAME "bridge01"
BLEService *pService;
BLECharacteristic *pCharacteristic;

// Function to initialize WiFi
void initWiFi()
{
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ");
  for (size_t i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++)
  {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi");
  Serial.println(WiFi.localIP());
  WiFi.disconnect();
}

String padStringToUUID(const String &str)
{
  String paddedStr = str;
  while (paddedStr.length() < 32)
  {
    paddedStr += "0";
  }

  // Insert hyphens at appropriate positions
  paddedStr = paddedStr.substring(0, 8) + "-" + paddedStr.substring(8, 12) + "-" +
              paddedStr.substring(12, 16) + "-" + paddedStr.substring(16, 20) + "-" +
              paddedStr.substring(20);

  return paddedStr;
}

String stringToHex(const String &str)
{
  String hexString;
  for (char c : str)
  {
    char buf[3];
    sprintf(buf, "%02X", static_cast<unsigned char>(c));
    hexString += buf;
  }
  return hexString;
}

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer) override
  {
    Serial.println("clients connected...");
  }

  void onDisconnect(BLEServer *pServer) override
  {
    Serial.println("clients disconnect...");
    BLEDevice::startAdvertising();
  }
};
// Function to initialize BLE
void initBLE()
{
  BLEDevice::init("ESP32-IOT-CAR");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(padStringToUUID(stringToHex(SERVICE_UUID_NAME)).c_str());
  pCharacteristic = pService->createCharacteristic(
      padStringToUUID(stringToHex(CHARACTERISTIC_UUID_NAME)).c_str(),
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);

  pCharacteristic->setValue("Hello World");
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(padStringToUUID(stringToHex(SERVICE_UUID_NAME)).c_str());
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  pServer->setCallbacks(new MyServerCallbacks());

  Serial.println("BLE service and characteristic started, advertising...");
}
// Function to handle BLE read/write requests
class MyCallbacks : public BLECharacteristicCallbacks
{
  std::string accumulatedValue;
  bool receiving = false;
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string value = pCharacteristic->getValue();
    Serial.print("BLE Write request received: ");
    Serial.println(value.c_str());
    // Check for the START marker
    // Check for the START marker at the beginning
    if (value.find("START") == 0)
    {
      accumulatedValue = "";
      receiving = true;
      value.erase(0, 5); // Remove the START marker
    }

    // Check for the END marker at the end
    if (!receiving)
    {
      accumulatedValue = "";
      accumulatedValue += value;
    }
    else if (value.rfind("END") == value.size() - 3)
    {
      value.erase(value.size() - 3); // Remove the END marker
      accumulatedValue += value;
      receiving = false;
    }
    else if (receiving)
    {
      accumulatedValue += value;
    }
    // Process the complete message if END marker is found
    if (!receiving && !accumulatedValue.empty())
    {
      Serial.print("Complete BLE message received: ");
      Serial.println(accumulatedValue.c_str());
    }
    // Process the complete message if END marker is found
    if (!receiving)
    {
      // Parse the JSON data
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, accumulatedValue);
      if (error)
      {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.f_str());
        Serial.println(accumulatedValue.c_str());
        pCharacteristic->setValue("Invalid JSON");
        pCharacteristic->notify();
        return;
      }

      const char *id = doc["id"];
      const char *url = doc["url"];
      const char *method = doc["method"];
      const char *body = doc["body"];
      JsonObject headers = doc["header"].as<JsonObject>();

      // Make HTTP client request based on the JSON data
      HTTPClient http;
      http.begin(url); // URL from JSON data

      // Add headers to the HTTP request
      for (JsonPair kv : headers)
      {
        http.addHeader(kv.key().c_str(), kv.value().as<const char *>());
      }

      int httpCode;
      if (strcmp(method, "GET") == 0)
      {
        httpCode = http.GET();
      }
      else if (strcmp(method, "POST") == 0)
      {
        httpCode = http.POST(body); // Body from JSON data
      }
      else
      {
        pCharacteristic->setValue("Unsupported HTTP method");
        pCharacteristic->notify();
        http.end();
        return;
      }

      String responsePayload;
      if (httpCode > 0)
      {
        responsePayload = http.getString();
      }
      else
      {
        responsePayload = "HTTP request failed";
      }

      // Extract headers from the HTTP response
      JsonObject resHeaders = doc.createNestedObject("header");
      for (int i = 0; i < http.headers(); i++)
      {
        resHeaders[http.headerName(i)] = http.header(i);
      }
      http.end();

      doc["status"] = httpCode;
      doc["body"] = responsePayload;

      // Serialize JSON back to string
      String response;
      serializeJson(doc, response);
      Serial.println(response);

      // Send the response in chunks of 512 bytes
      const size_t chunkSize = 512;

      if (response.length() <= chunkSize)
      {

        pCharacteristic->setValue(response.c_str());
        pCharacteristic->notify();
      }
      else
      {
        size_t responseLength = response.length();
        size_t sentLength = 0;
        while (sentLength < responseLength)
        {
          size_t currentChunkSize = (responseLength - sentLength) > chunkSize ? chunkSize : (responseLength - sentLength);
          String chunk = response.substring(sentLength, sentLength + currentChunkSize);

          // Add start and end markers
          if (sentLength == 0)
          {
            chunk = "START" + chunk;
          }
          if (sentLength + currentChunkSize == responseLength)
          {
            chunk += "END";
          }

          pCharacteristic->setValue(chunk.c_str());
          pCharacteristic->notify();
          sentLength += currentChunkSize;
          delay(20); // Short delay to ensure the transmission is processed
        }
      }
    }
  }

  void onStatus(BLECharacteristic *pCharacteristic, Status s, uint32_t code) override
  {
    Serial.println("onStatus...");
  }
};

void moveForward(int speed)
{
  Serial.println("moveForward");
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, speed);
  digitalWrite(BIN1, HIGH);
  digitalWrite(BIN2, LOW);
  analogWrite(PWMB, speed);
}

void moveBackward(int speed)
{
  Serial.println("moveBackward");
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
  analogWrite(PWMA, speed);
  digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, HIGH);
  analogWrite(PWMB, speed);
}

void turnLeft(int speed)
{
  Serial.println("turnLeft");
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
  analogWrite(PWMA, speed);
  digitalWrite(BIN1, HIGH);
  digitalWrite(BIN2, LOW);
  analogWrite(PWMB, speed);
}

void turnRight(int speed)
{
  Serial.println("turnRight");
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, speed);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, HIGH);
  analogWrite(PWMB, speed);
}

void stop()
{
  Serial.println("stop");
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, 0);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);
  analogWrite(PWMB, 0);
}

void initMotor()
{

  // 打印初始信息
  Serial.println("inin pin...");
  // 设置引脚模式
  pinMode(STBY, OUTPUT);
  pinMode(PWMA, OUTPUT);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(PWMB, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  // 启动驱动板
  digitalWrite(STBY, HIGH);
}

void initWebServer()
{
  // 定义HTTP请求处理函数
  static const char s_content_enc[] PROGMEM = "Content-Encoding";
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", PAGE_index, PAGE_index_L);
    response->addHeader(s_content_enc,"gzip");
    request->send(response); });

  server.on("/forward", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    moveForward(255);
    request->send(200, "text/plain", "Moving forward"); });

  server.on("/backward", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    moveBackward(255);
    request->send(200, "text/plain", "Moving backward"); });

  server.on("/left", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    turnLeft(255);
    request->send(200, "text/plain", "Turning left"); });

  server.on("/right", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    turnRight(255);
    request->send(200, "text/plain", "Turning right"); });

  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    stop();
    request->send(200, "text/plain", "Stopped"); });

  // 启动服务器
  server.begin();
}
void receive_ir_data()
{
  if (IrReceiver.decode())
  {
    Serial.print(F("Decoded protocol: "));
    Serial.print(getProtocolString(IrReceiver.decodedIRData.protocol));
    Serial.print(F(", decoded raw data: "));
#if (__INT_WIDTH__ < 32)
    Serial.print(IrReceiver.decodedIRData.decodedRawData, HEX);
#else
    PrintULL::print(&Serial, IrReceiver.decodedIRData.decodedRawData, HEX);
#endif
    Serial.print(F(", decoded address: "));
    Serial.print(IrReceiver.decodedIRData.address, HEX);
    Serial.print(F(", decoded command: "));
    Serial.println(IrReceiver.decodedIRData.command, HEX);
    if (IrReceiver.decodedIRData.command == 0x18)
    {
      moveForward(255);
    }
    else if (IrReceiver.decodedIRData.command == 0x52)
    {
      moveBackward(255);
    }
    else if (IrReceiver.decodedIRData.command == 0x8)
    {
      turnLeft(255);
    }
    else if (IrReceiver.decodedIRData.command == 0x5A)
    {
      turnRight(255);
    }
    else if (IrReceiver.decodedIRData.command == 0x1C)
    {
      stop();
    }

    IrReceiver.resume();
  }
}

void setup()
{
  // 初始化串口通信，波特率设为9600
  Serial.begin(115200);
  initMotor();
  // Initialize BLE
  initBLE();

  // Set BLE characteristic callbacks
  pCharacteristic->setCallbacks(new MyCallbacks());
  // Connect to Wi-Fi
  initWiFi();

  initWebServer();

  // 启动红外接收器
  irrecv.enableIRIn();
}
void loop()
{
  // receive_ir_data();
  delay(100); // 短暂延时，防止串口输出过快
}
