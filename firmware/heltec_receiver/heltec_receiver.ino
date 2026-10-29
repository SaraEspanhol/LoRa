#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <SSD1306Wire.h>
#include <LoRa.h>

// OLED - Heltec ESP32 V2
#define SDA_OLED 4
#define SCL_OLED 15
#define RST_OLED 16
SSD1306Wire display(0x3c, SDA_OLED, SCL_OLED);

#define GPS_BAUD 9600
// #define GPS_RX   32
// #define GPS_TX   33

#define RECEPTOR_ID "DRONE"

const char* ssid = "SUA_REDE"; // Colocar ssid da Rede WiFi
const char* password = "SUA_SENHA"; // Colocar Senha da Rede WiFi
const char* server_ip = "192.168.XX.XX"; // Colocar IP do Computador
const uint16_t server_port = 5001;

// TinyGPSPlus gps;
// HardwareSerial GPSserial(2);
WiFiClient client;

// uint32_t lastSentenceMs = 0;
// uint32_t lastReport = 0;
unsigned long reportInterval = 5000;

void logOLED(const String& linha1, const String& linha2 = "", const String& linha3 = "") {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, linha1);
  if (linha2 != "") display.drawString(0, 12, linha2);
  if (linha3 != "") display.drawString(0, 24, linha3);
  display.display();
}

void logBanner(const __FlashStringHelper* msg) {
  Serial.println(F("=============================="));
  Serial.println(msg);
  Serial.println(F("=============================="));
}

void setup() {
  // Inicializa OLED
  pinMode(RST_OLED, OUTPUT);
  digitalWrite(RST_OLED, LOW);
  delay(50);
  digitalWrite(RST_OLED, HIGH);
  display.init();
  display.flipScreenVertically();

  logOLED("[TAREFA]", "Inicializando dispositivo...");
  Serial.begin(115200);
  delay(1500);

  logBanner(F("[TAREFA] Inicializando dispositivo..."));
  // GPSserial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);

  // Inicializa WiFi - 20 tentativas
  logBanner(F("Conectando ao Wi-Fi"));
  logOLED("[TAREFA]", "Conectando ao Wi-Fi:", ssid);
  WiFi.begin(ssid, password);
  uint8_t retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  //Valida WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[ERRO] Wi-Fi não conectou."));
    logOLED("[ERRO]", "Wi-Fi não conectou.");
    while(1);
  }
  Serial.print(F("[OK] Conectado ao Wi-Fi. IP: "));
  Serial.println(WiFi.localIP());
  logOLED("[OK]", "Conectado ao Wi-Fi.");
  delay(1500);
  
  //Inicializa servidor
  connectServer();

  // Inicialização do LoRa
  Serial.println("[TAREFA] Iniciando LoRa...");
  logOLED("[TAREFA]", "Iniciando LoRa...");
  LoRa.setPins(18, 14, 26); // NSS, RST, DIO0
  if (!LoRa.begin(915E6)) {
    logOLED("[ERRO]", "LoRa não iniciado");
    while(1) delay(1000);
  }
  delay(1500);
  LoRa.setSpreadingFactor(12);
  Serial.println("[OK] LoRa iniciado.");
  logOLED("[OK]", "LoRa iniciado.", "Aguardando pacotes...");
}

void connectServer() {
  logBanner(F("Iniciando conexão com servidor TCP"));
  logOLED("[TAREFA]", "Iniciando conexão com \nservidor TCP");
  delay(1500);
  if (!client.connect(server_ip, server_port)) {
    Serial.println(F("[ERRO] Falha ao conectar ao servidor TCP"));
    logOLED("[ERRO]", "Falha ao conectar ao \nservidor TCP");
    Serial.println(WiFi.localIP());
    while (!client.connect(server_ip, server_port)) delay(1000);
  }
  Serial.println(F("[OK] Conectado ao servidor TCP"));
  logOLED("[OK]", "Conectado ao servidor TCP");
  delay(1500);
}

// void printHDOPStatus(float hdop) {
//   if (hdop == 0.0 || isnan(hdop)) {
//     Serial.println(F("[GPS] HDOP inválido."));
//     return;
//   }

//   Serial.print("[GPS] Qualidade do sinal (HDOP): ");
//   Serial.print(hdop, 1);
//   Serial.print(" → ");

//   if (hdop <= 1.0) Serial.println(F("ÓTIMO"));
//   else if (hdop <= 2.0) Serial.println(F("BOM"));
//   else if (hdop <= 4.0) Serial.println(F("REGULAR"));
//   else Serial.println(F("RUIM"));
// }

// void printGPSTime() {
//   if (gps.time.isValid() && gps.date.isValid()) {
//     Serial.printf("[GPS] Hora UTC: %02d:%02d:%02d - %02d/%02d/%04d\n",
//       gps.time.hour(), gps.time.minute(), gps.time.second(),
//       gps.date.day(), gps.date.month(), gps.date.year());
//   } else {
//     Serial.println(F("[GPS] Hora UTC: inválida ou ainda não recebida."));
//   }
// }

void sendTCP(const String& msg, const int rssi) {
  String packet = String(msg) + "[" + RECEPTOR_ID + "]" + "[" + String(rssi) + "]";

  if (client.connected()) {
    client.println(packet);  
    } else {
    Serial.println(F("[ERRO] TCP desconectado."));
    logOLED("[ERRO] TCP desconectado. Iniciando reconexão...");
    delay(3000);
    connectServer();
  }
}

void sendHeartBeat(){
  String heartBeat = "HEARTBEAT_DRONE";
  client.println(heartBeat);  
}

// Função auxiliar para extrair valores entre colchetes
String getValue(String data, int index) {
  int start = -1;
  int end = -1;
  int count = 0;

  for (int i = 0; i < data.length(); i++) {
    if (data.charAt(i) == '[') {
      if (count == index) start = i + 1;
      count++;
    } else if (data.charAt(i) == ']' && count == index + 1) {
      end = i;
      break;
    }
  }

  if (start != -1 && end != -1) {
    return data.substring(start, end);
  } else {
    return "";
  }
}

unsigned long lastHeartbeat = 0;
unsigned long heartbeatInterval = 5000;

void loop() {
  //Tratando pacotes LoRa
    if (millis() - lastHeartbeat > heartbeatInterval) {
    sendHeartBeat();
    lastHeartbeat = millis();
  }
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String msg = "";
    while (LoRa.available()) {
      msg += (char)LoRa.read();
    }

    int rssi = LoRa.packetRssi();
    Serial.println("Recebido: " + msg);
    Serial.println("RSSI: " + String(rssi));

    //Enviando dados para o servidor
    logOLED("Novo pacote recebido:", msg);
    sendTCP(msg, rssi);
  }
}
