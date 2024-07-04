#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <IRremote.h>
#include "html_content.h" // 包含生成的头文件
// 引脚定义
const int STBY = 17; // 使能引脚
const int PWMA = 12; // 电机A速度控制
const int AIN1 = 14; // 电机A方向控制
const int AIN2 = 13;
const int PWMB = 25; // 电机B速度控制
const int BIN1 = 26; // 电机B方向控制
const int BIN2 = 27;
const char *ssid = "XY421253";
const char *password = "12345678";
AsyncWebServer server(80);
// 定义红外接收引脚
const int RECV_PIN = 19;

// 创建一个红外接收对象
IRrecv irrecv(RECV_PIN);
decode_results results;

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

void setup()
{
  // 初始化串口通信，波特率设为9600
  Serial.begin(9600);

  // 打印初始信息
  Serial.println("inin");
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

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());
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
  // 启动红外接收器
  irrecv.enableIRIn();
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
void loop()
{
  receive_ir_data();
  delay(100); // 短暂延时，防止串口输出过快
}
