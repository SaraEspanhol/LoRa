#include <SPI.h>
#include <LoRa.h>
#include <SSD1306Wire.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// Configurações GPS
#define GPS_BAUD 9600
#define GPS_RX   32
#define GPS_TX   33

TinyGPSPlus gps;
HardwareSerial GPSserial(2);
uint32_t lastSentenceMs = 0;
uint32_t lastReport = 0;
unsigned long reportInterval = 5000;

// Configurações OLED para Heltec ESP32 V2
#define SDA_OLED 4
#define SCL_OLED 15
#define RST_OLED 16

SSD1306Wire display(0x3c, SDA_OLED, SCL_OLED); // Fonte padrão
int counter = 0;


// =========================================================
// Simulador de GPS (para testes sem módulo físico conectado)
// =========================================================
struct FakeGPS {
  float lat;
  float lon;
  float altitude;
  float hdop;
  int hour, minute, second;
  float velocidade_ms;  // velocidade média em m/s
  float direcao_deg;    // direção do movimento em graus (0 = norte, 90 = leste)
} fakeGPS;

// Mudar para false quando não estiver usando o FakeGPS!
bool useFakeGPS = false;

unsigned long lastFakeUpdate = 0;
unsigned long lastTurn = 0;

void initFakeGPS() {
  // Posição inicial: Campinas - SP (aprox)
  fakeGPS.lat = -22.909938;
  fakeGPS.lon = -47.062633;
  fakeGPS.altitude = 680.0;
  fakeGPS.hdop = 1.2;

  fakeGPS.velocidade_ms = 11.1;   // ~40 km/h
  fakeGPS.direcao_deg = 90.0;     // andando inicialmente para leste

  fakeGPS.hour = 12;
  fakeGPS.minute = 0;
  fakeGPS.second = 0;
}

void updateFakeGPS() {
  unsigned long now = millis();

  // Atualiza posição a cada 2s
  if (now - lastFakeUpdate > 2000) {
    lastFakeUpdate = now;

    // Distância percorrida em 2s
    float distancia = fakeGPS.velocidade_ms * 2.0; // metros

    // Converte direção para radianos
    float direcao_rad = radians(fakeGPS.direcao_deg);

    // Conversões aproximadas
    float deltaLat = (distancia * cos(direcao_rad)) / 111320.0;
    float deltaLon = (distancia * sin(direcao_rad)) / (111320.0 * cos(radians(fakeGPS.lat)));

    fakeGPS.lat += deltaLat;
    fakeGPS.lon += deltaLon;

    // Altitude com pequena oscilação
    fakeGPS.altitude += (random(-5, 6)) * 0.05;

    // Simula hora UTC correndo
    fakeGPS.second += 2;
    if (fakeGPS.second >= 60) {
      fakeGPS.second = 0;
      fakeGPS.minute++;
      if (fakeGPS.minute >= 60) {
        fakeGPS.minute = 0;
        fakeGPS.hour++;
        if (fakeGPS.hour >= 24) fakeGPS.hour = 0;
      }
    }

    // Simula variação no HDOP
    fakeGPS.hdop = 0.8 + random(0, 15) / 10.0; // 0.8 até 2.2
  }

  // A cada 60s, muda direção aleatoriamente (como se virasse uma rua)
  if (now - lastTurn > 60000) {
    lastTurn = now;
    int delta = random(-90, 91); // vira entre -90° e +90°
    fakeGPS.direcao_deg += delta;
    if (fakeGPS.direcao_deg < 0) fakeGPS.direcao_deg += 360;
    if (fakeGPS.direcao_deg >= 360) fakeGPS.direcao_deg -= 360;

    Serial.print(F("[FAKE GPS] Mudando direção em "));
    Serial.print(delta);
    Serial.println(F(" graus"));
  }
}

float chanceErro = 0.4; // 40% de chance de corromper o pacote
bool pacoteCorrompido = true;

String corromperPacote(const String &msgOriginal) {
  String msg = msgOriginal;
  pacoteCorrompido = false; // reset a cada chamada

  if ((random(0, 1000) / 1000.0) < chanceErro) {
    pacoteCorrompido = true;

    int tipo = random(0, 5); // diferentes modos de corrupção
    switch(tipo) {
      case 0: // Remove grande parte do pacote
        if (msg.length() > 10) {
          int start = random(0, msg.length()/2);
          int len = random(5, msg.length()/2);
          msg.remove(start, len);
        }
        break;
      case 1: // Embaralha blocos
        {
          int cut1 = random(0, msg.length()/2);
          int cut2 = random(cut1, msg.length());
          String block = msg.substring(cut1, cut2);
          msg.remove(cut1, cut2 - cut1);
          int insertPos = random(0, msg.length());
          msg = msg.substring(0, insertPos) + block + msg.substring(insertPos);
        }
        break;
      case 2: // Troca números e letras aleatórios
        for (int i = 0; i < msg.length(); i++) {
          if (random(0,2)==0) {
            if (isDigit(msg[i])) msg[i] = '0' + random(0,10);
            else if (isAlpha(msg[i])) msg[i] = 'A' + random(0,26);
          }
        }
        break;
      case 3: // Insere caracteres não imprimíveis
        for (int i = 0; i < random(1,5); i++) {
          int pos = random(0, msg.length());
          char c = (char)random(1,32); // ASCII control
          msg = msg.substring(0,pos) + c + msg.substring(pos);
        }
        break;
      case 4: // Duplicar trechos
        if (msg.length() > 5) {
          int start = random(0, msg.length()/2);
          int len = random(2, msg.length()/2);
          String dup = msg.substring(start, start+len);
          int insertPos = random(0, msg.length());
          msg = msg.substring(0, insertPos) + dup + msg.substring(insertPos);
        }
        break;
    }
    Serial.println(F("[FAKE GPS] Pacote fortemente corrompido para teste!"));
  }

  return msg;
}

bool fakeGPSisValid() {
  return true; // Sempre válido no modo simulado
}

// =========================================================
// Fim do Simulador de GPS (Gerado por IA)
// =========================================================


void printGPSTime() {
  if (gps.time.isValid() && gps.date.isValid()) {
    char buffer[32];
    sprintf(buffer, "%02d:%02d:%02d %02d/%02d/%04d",
            gps.time.hour(), gps.time.minute(), gps.time.second(),
            gps.date.day(), gps.date.month(), gps.date.year());
    display.drawString(0, 48, String(buffer));
  } else {
    display.drawString(0, 48, "Hora UTC: invalida");
  }
}

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

void printHDOPStatus(float hdop) {
  if (hdop == 0.0 || isnan(hdop)) {
    Serial.println(F("[GPS] HDOP inválido."));
    return;
  }

  Serial.print("[GPS] Qualidade do sinal (HDOP): ");
  Serial.print(hdop, 1);
  Serial.print(" → ");

  if (hdop <= 1.0) Serial.println(F("ÓTIMO"));
  else if (hdop <= 2.0) Serial.println(F("BOM"));
  else if (hdop <= 4.0) Serial.println(F("REGULAR"));
  else Serial.println(F("RUIM"));
}

void setup() {
  // Inicializa OLED
  pinMode(RST_OLED, OUTPUT);
  digitalWrite(RST_OLED, LOW);
  delay(50);
  digitalWrite(RST_OLED, HIGH);
  display.init();
  display.flipScreenVertically();
  
  if (useFakeGPS) {
    initFakeGPS();
    Serial.println(F("[FAKE GPS] Ativado"));
  }

  logOLED("[TAREFA]", "Inicializando dispositivo...");
  logBanner(F("[TAREFA] Inicializando dispositivo..."));

  Serial.begin(115200);
  delay(1500);

  // Inicializa o LoRa
  Serial.println("[TAREFA] Iniciando LoRa...");
  logOLED("[TAREFA]", "Iniciando LoRa...");
  delay(1500);
  LoRa.setPins(18, 14, 26); // NSS, RST, DIO0
  if (!LoRa.begin(915E6)) {
    logOLED("[ERRO]", "LoRa não iniciado");
    logBanner(F("[ERRO] LoRa não iniciado"));
    while(1);
  }
  delay(1500);
  LoRa.setSpreadingFactor(12);
  Serial.println("[OK] LoRa iniciado.");
  logOLED("[OK]", "LoRa iniciado.", "Aguardando dados de GPS...");
  LoRa.setTxPower(20);
    
  // Inicializa GPS
  GPSserial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  if (!GPSserial) {
    logBanner(F("[ERRO] Falha ao iniciar GPS - Verifique conexões"));
    logOLED("[ERRO]", "Falha ao iniciar GPS", "Verifique conexões");
    while(1);
  }
}

void loop() {
  while (GPSserial.available()) {
    char c = GPSserial.read();
    gps.encode(c);
    lastSentenceMs = millis();
  }

  if (millis() - lastSentenceMs > 10000) {
    Serial.println(F("[ERRO] >10 s sem NMEA! Verifique conexão e antena."));
    lastSentenceMs = millis();
  }

  if (millis() - lastReport >= reportInterval) {
    lastReport = millis();

    if(gps.location.isValid() && !useFakeGPS) {
      // Monta a mensagem LoRa
      String msg = String(counter++) + "[BRAVO]";

      // Envia via LoRa
      float lat = gps.location.lat();
      float lon = gps.location.lng();
      msg += "[" + String(lat, 6) + "][" + String(lon, 6) + "]";

      float sinal = 0.0;
      if (gps.hdop.isValid()) {
      sinal = gps.hdop.hdop();
        msg += "[" + String(sinal) + "]";
      }

      if (gps.altitude.isValid()) {
        msg += "[" + String(gps.altitude.meters(), 1) + "]";
      }

      if (gps.time.isValid()) {
        msg += "[";
        msg += String(gps.time.hour()) + ":" +
              String(gps.time.minute()) + ":" +
              String(gps.time.second()) + "]";
      }

      //if (!gps.location.isValid() && !gps.altitude.isValid() && !gps.time.isValid()) {
      //  msg = String(counter++) + "Mensagem Teste";
      //}

      // Envia via LoRa
      LoRa.beginPacket();
      LoRa.print(msg);
      LoRa.endPacket();

      // Mostra resumo no OLED (linha por linha)
      String result = "[LoRa] pacote enviado \nLat: " + String(lat) + "\nLon: " + String(lon) + "\nSinal: " + String(sinal);
      logOLED(result);
      Serial.println("Enviado: " + msg);

      delay(8000); // Aguarda 8 segundos entre envios
    }
    else if(!useFakeGPS){
      String qsinal = "Qualidade do sinal: " + String(gps.hdop.hdop());
      logBanner(F("[ERRO] Localização do GPS inválida - Aguarde calibração"));
      printGPSTime();
      logOLED("[ERRO]", "Localização do GPS inválida", "Aguarde calibração\n" + qsinal);
    } else{
      // usa GPS fake
      updateFakeGPS();
      float lat = fakeGPS.lat;
      float lon = fakeGPS.lon;

      String msg = String(counter++) + "[IFSPC@MPINAS]";
      msg += "[" + String(lat, 6) + "][" + String(lon, 6) + "]";
      msg += "[" + String(fakeGPS.hdop, 1) + "]";
      msg += "[" + String(fakeGPS.altitude, 1) + "]";
      msg += "[" + String(fakeGPS.hour) + ":" +
                  String(fakeGPS.minute) + ":" +
                  String(fakeGPS.second) + "]";

      String msgToSend = corromperPacote(msg);
      LoRa.beginPacket();
      LoRa.print(msgToSend);
      LoRa.endPacket();

      String result = "[LoRa] pacote enviado \nLat: " + String(lat, 6) +
                      "\nLon: " + String(lon, 6) +
                      "\nSinal: " + String(fakeGPS.hdop, 1);

      if (pacoteCorrompido) {
        result += "\nPacote corrompido!";
      }
      logOLED(result);
      Serial.println("Enviado (FAKE): " + msgToSend);

      delay(10000);
    }
  }
}
