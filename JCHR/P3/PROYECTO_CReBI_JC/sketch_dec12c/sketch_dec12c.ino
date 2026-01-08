#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT11.h>

// =====================
// DHT11 (dos sensores)
// =====================
DHT11 dht11_in(38);    // Sensor interior
DHT11 dht11_out(4);    // Sensor exterior

// =====================
// Pines de alarma y relé
// =====================
#define ALARM_PIN 5
#define TEMP_ALARM 60
#define RELAY_PIN 18

// =====================
// WiFi / MQTT
// =====================
const char* ssid = "Galaxy A50";
const char* password = "casa8080**";
const char* mqttServer = "test.mosquitto.org";
const int mqttPort = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// =====================
// Variables para parpadeo
// =====================
unsigned long lastBlink = 0;
bool ledState = false;
const unsigned long blinkInterval = 500; // ms

// =====================
// Setup
// =====================
void setup() {
  Serial.begin(115200);

  // ---------- Configura Pin Alarma ----------
  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(ALARM_PIN, LOW); // alarma apagada

  // ---------- Configura Pin Rele ----------
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // relé apagado (activo LOW)

  // ---------- Conexión WiFi ----------
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");

  // ---------- Conexión MQTT ----------
  client.setServer(mqttServer, mqttPort);
  while (!client.connected()) {
    Serial.println("Conectando a broker MQTT...");
    if (client.connect("jc_iot_2025")) {
      Serial.println("Conectado a broker MQTT");
    } else {
      Serial.print("Error MQTT: ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

// =====================
// Loop
// =====================
void loop() {
  client.loop();

  // -------- Leer sensores --------
  int temp_in = dht11_in.readTemperature();
  int hum_in  = dht11_in.readHumidity();

  int temp_out = dht11_out.readTemperature();
  int hum_out  = dht11_out.readHumidity();

  // -------- Lógica alarma y relé ----------
  bool alarmActive = (temp_in >= TEMP_ALARM) || (temp_out >= TEMP_ALARM);

  if (alarmActive) {
    // -------- Parpadeo LED usando millis() --------
    unsigned long now = millis();
    if (now - lastBlink >= blinkInterval) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(ALARM_PIN, ledState);
    }
    // Relé encendido mientras hay alarma
    digitalWrite(RELAY_PIN, LOW);  // RELÉ ON
  } else {
    digitalWrite(ALARM_PIN, LOW);
    digitalWrite(RELAY_PIN, HIGH); // RELÉ OFF
  }

  // -------- Mostrar datos --------
  Serial.println("=== SENSOR INTERIOR ===");
  Serial.print("Temp: "); Serial.print(temp_in); Serial.println(" °C");
  Serial.print("Hum:  "); Serial.print(hum_in);  Serial.println(" %");

  Serial.println("=== SENSOR EXTERIOR ===");
  Serial.print("Temp: "); Serial.print(temp_out); Serial.println(" °C");
  Serial.print("Hum:  "); Serial.print(hum_out);  Serial.println(" %");

  // -------- Envío MQTT --------
  if ((hum_in >= 0 && hum_in <= 100) && (temp_in >= -20 && temp_in <= 100)) {
    char payload_in[64];
    snprintf(payload_in, sizeof(payload_in),
             "{\"temp\":%d,\"hum\":%d}", temp_in, hum_in);
    client.publish("jc_iot_2025/in", payload_in);
  }

  if ((hum_out >= 0 && hum_out <= 100) && (temp_out >= -20 && temp_out <= 100)) {
    char payload_out[64];
    snprintf(payload_out, sizeof(payload_out),
             "{\"temp\":%d,\"hum\":%d}", temp_out, hum_out);
    client.publish("jc_iot_2025/out", payload_out);
  }

  // -------- Espera antes de la siguiente lectura --------
  delay(1000); // pequeño delay para estabilidad
}

