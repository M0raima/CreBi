#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT11.h>
#include <ArduinoJson.h>

// =====================
// DHT11
// =====================
DHT11 dht11_in(38);    // Interior
DHT11 dht11_out(4);    // Exterior

// =====================
// Pines
// =====================
#define ALARM_PIN 5     // LED estado FAN (activo HIGH)
#define RELAY_PIN 18    // Relé FAN (activo LOW)

// =====================
// WiFi / MQTT
// =====================
const char* ssid = "CReBINet";
const char* password = "12345678";

// ===================== MQTT =====================
const char* TOPIC_PUB = "NAPIoT/P3_01";
const char* TOPIC_SUB = "NAPIoT/P3_02";

// Brokers (prioridad)
const char* FOG_HOST     = "192.168.1.90";
const uint16_t FOG_PORT  = 1883;

const char* CL_HOST      = "192.168.1.74";
const uint16_t CL_PORT   = 1883;

const char* MQTT_USER = "";
const char* MQTT_PASS = "";

// Identidad del dispositivo (para filtrar comandos si quieres)
const char* DEVICE_ID = "esp_01";

WiFiClient espClient;
PubSubClient client(espClient);

// =====================
// Estados
// =====================
bool fanState = false;
bool controlManual = false;
bool bloqueoServo = false;
bool alarmaExteriorActiva = false;

// =====================
// Setup
// =====================
void setup() {
  Serial.begin(115200);

  pinMode(ALARM_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(ALARM_PIN, LOW);
  digitalWrite(RELAY_PIN, HIGH);   // OFF (activo LOW)

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  mqtt.setCallback(onMqttMessage);

  ensureWiFi();
  connectFailover();
}

// =====================
// Loop
// =====================
void loop() {
  mqttService();

  if (!client.connected()) reconnect();
  client.loop();

  // ---- Lectura sensores ----
  int temp_in  = dht11_in.readTemperature();
  int hum_in   = dht11_in.readHumidity();
  int temp_out = dht11_out.readTemperature();
  int hum_out  = dht11_out.readHumidity();

  // =====================
  // Monitor sensores (SEPARADOS)
  // =====================
  Serial.println("--- SENSOR INTERIOR ---");
  Serial.print("Temp: "); Serial.print(temp_in); Serial.println(" °C");
  Serial.print("Hum : "); Serial.print(hum_in);  Serial.println(" %");

  Serial.println("--- SENSOR EXTERIOR ---");
  Serial.print("Temp: "); Serial.print(temp_out); Serial.println(" °C");
  Serial.print("Hum : "); Serial.print(hum_out);  Serial.println(" %");

  // =====================
  // Publicar MQTT (1 paquete)
  // =====================
  StaticJsonDocument<256> doc;
  doc["interior"]["temp"] = temp_in;
  doc["interior"]["hum"]  = hum_in;
  doc["exterior"]["temp"] = temp_out;
  doc["exterior"]["hum"]  = hum_out;

// Enviar cada 2s a NAPIoT/P3_01
  static unsigned long lastSend = 0;
  unsigned long now = millis();
  if (now - lastSend > 2000) {
    lastSend = now;

  char payload[256];
  serializeJson(doc, payload);


  publishTelemetry(payload);

  // =====================
  // Evaluar alarmas
  // =====================
  bool alarmaTempInt = temp_in > 30;
  bool alarmaHumInt  = hum_in  > 75;
  bool alarmaTempExt = temp_out > 50;
  bool alarmaHumExt  = hum_out  > 90;

  bool alarmaInterior = alarmaTempInt && alarmaHumInt;
  bool alarmaExterior = alarmaTempExt || alarmaHumExt;

  // =====================
  // Mensajes SOLO EN ALARMA
  // =====================
  if (alarmaInterior) {
    Serial.println("🚨 ALERTA INTERIOR");
    if (alarmaTempInt) Serial.println(" - Temperatura interior ALTA");
    if (alarmaHumInt)  Serial.println(" - Humedad interior ALTA");
  }

  if (alarmaExterior) {
    Serial.println("🚨 ALERTA EXTERIOR");
    if (alarmaTempExt) Serial.println(" - Temperatura exterior ALTA");
    if (alarmaHumExt)  Serial.println(" - Humedad exterior ALTA");

    if (!alarmaExteriorActiva) {
      client.publish("NAPIoT/P3_ALERTAS", "ALERTA EXTERIOR");
      alarmaExteriorActiva = true;
    }
  } else {
    alarmaExteriorActiva = false;
  }

  // =====================
  // Control FAN automático
  // =====================
  if (!controlManual && !bloqueoServo) {
    fanState = alarmaInterior || alarmaExterior;
  }

  // =====================
  // Aplicar FAN + LED (SOLO SI HAY ALARMA)
  // =====================
  if (fanState) {
    digitalWrite(RELAY_PIN, LOW);    // FAN ON
    digitalWrite(ALARM_PIN, HIGH);   // LED ON
    Serial.println("➡ FAN ENCENDIDO");
    Serial.println("➡ LED ENCENDIDO");
  } else {
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(ALARM_PIN, LOW);
  }

  Serial.println();
  delay(1000);
}

// =====================
// Callback MQTT
// =====================
void callback(char* topic, byte* payload, unsigned int length) {
 if (strcmp(topic, TOPIC_SUB) != 0) return;

  // Copia payload a buffer con terminador NULL
  static char buf[512];
  if (length >= sizeof(buf)) {
    // payload demasiado grande
    publishAck(mqtt, "", "", 0, "error");
    return;
  }
  memcpy(buf, payload, length);
  buf[length] = '\0';

  // Parse JSON
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, buf);
  if (err) {
    publishAck(mqtt, "", "", 0, "error");
    return;
  }

  const char* type   = doc["type"]   | "";
  const char* cmd_id = doc["cmd_id"] | "";
  const char* src    = doc["src"]    | "";
  const char* dst    = doc["dst"]    | "";
  const char* act    = doc["act"]    | "";
  int value           = doc["value"] | 0;

  // 1) Solo comandos
  if (strcmp(type, "cmd") != 0) return;

  // 2) Solo si va dirigido a mí (o broadcast)
  bool forMe = (strcmp(dst, DEVICE_ID) == 0) || (strcmp(dst, "all") == 0) || (strcmp(dst, "broadcast") == 0);
  if (!forMe) return;

  // 3) Evita eco propio (por si algún día este nodo publica cmd)
  if (strcmp(src, DEVICE_ID) == 0) return;

  // 4) Deduplicación por cmd_id
  if (alreadySeenCmdId(cmd_id)) {
    publishAck(mqtt, cmd_id, act, value, "ignored");
    return;
  }
  rememberCmdId(cmd_id);

  // 5) Ejecuta
  bool ok = executeActuator(act, value);
  publishAck(mqtt, cmd_id, act, value, ok ? "ok" : "error");
}
bool executeActuator(const char* act, int value) {
  if (!act) return false;

  // Ejemplo: servo
  if (strcmp(act, "servo") == 0) {
    if (act == "ON") {
      fanState = true;
      controlManual = true;
    }
    if (act == "OFF") {
      fanState = false;
      controlManual = true;
    }
    if (act == "AUTO") {
      controlManual = false;
    }
    return true;
  }
}

// =====================
// Reconnect MQTT
// =====================
void reconnect() {
  while (!client.connected()) {
    if (client.connect("JCHR")) {
      client.subscribe("NAPIoT/P3_02");
      client.subscribe("NAPIoT/SERVO");
    } else {
      delay(5000);
    }
  }
}
