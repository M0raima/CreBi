#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT11.h>

// =====================
// DHT11 (dos sensores)
// =====================
DHT11 dht11_in(38);    // Sensor interior
DHT11 dht11_out(4);    // Sensor exterior

// =====================
// Pines y umbrales
// =====================
#define ALARM_PIN 5
#define RELAY_PIN 18
#define TEMP_ALARM 60   // °C

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
// Variables de control
// =====================
unsigned long lastBlink = 0;
bool ledState = false;
const unsigned long blinkInterval = 500;

bool relayState = false;  // false = OFF, true = ON

// =====================
// Setup
// =====================
void setup() {
  Serial.begin(115200);

  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(ALARM_PIN, LOW);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // RELÉ OFF (activo HIGH)

  // ---- WiFi ----
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");

  // ---- MQTT ----
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

  // ---- Lectura sensores ----
  int temp_in = dht11_in.readTemperature();
  int hum_in  = dht11_in.readHumidity();

  int temp_out = dht11_out.readTemperature();
  int hum_out  = dht11_out.readHumidity();

  // ---- Lógica alarma ----
  bool alarmActive = (temp_in >= TEMP_ALARM) || (temp_out >= TEMP_ALARM);

  // ---- Activar relé y mensaje ----
  if (alarmActive && !relayState) {
    relayState = true;
    digitalWrite(RELAY_PIN, HIGH);  // FAN ON
    Serial.println("🚨 ALARMA DE TEMPERATURA ALTA - ENCENDER FAN");
  }

  // ---- Desactivar relé y mensaje ----
  if (!alarmActive && relayState) {
    relayState = false;
    digitalWrite(RELAY_PIN, LOW);   // FAN OFF
    Serial.println("✅ Temperatura normal - FAN APAGADO");
  }

  // ---- LED parpadeante ----
  if (relayState) {
    unsigned long now = millis();
    if (now - lastBlink >= blinkInterval) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(ALARM_PIN, ledState);
    }
  } else {
    digitalWrite(ALARM_PIN, LOW);
  }

  // ---- Monitor Serial ----
  Serial.println("=== SENSOR INTERIOR ===");
  Serial.print("Temp: "); Serial.print(temp_in); Serial.println(" °C");
  Serial.print("Hum:  "); Serial.print(hum_in);  Serial.println(" %");

  Serial.println("=== SENSOR EXTERIOR ===");
  Serial.print("Temp: "); Serial.print(temp_out); Serial.println(" °C");
  Serial.print("Hum:  "); Serial.print(hum_out);  Serial.println(" %");

  // ---- MQTT ----
  if (hum_in >= 0 && hum_in <= 100 && temp_in >= -20 && temp_in <= 100) {
    char payload_in[64];
    snprintf(payload_in, sizeof(payload_in),
             "{\"temp\":%d,\"hum\":%d}", temp_in, hum_in);
    client.publish("jc_iot_2025/in", payload_in);
  }

  if (hum_out >= 0 && hum_out <= 100 && temp_out >= -20 && temp_out <= 100) {
    char payload_out[64];
    snprintf(payload_out, sizeof(payload_out),
             "{\"temp\":%d,\"hum\":%d}", temp_out, hum_out);
    client.publish("jc_iot_2025/out", payload_out);
  }

  delay(1000);
}

