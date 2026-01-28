#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// =================================================
// PINES DE HARDWARE
// =================================================
#define PIN_IR        8     // Sensor de presencia
#define PIN_SERVO     10    // Servo de la tapa
#define PIN_CRASH     6     // Sensor de cierre

// =================================================
// SERVO
// =================================================
Servo servoTapa;
const int ANGULO_CERRADO = 0;
const int ANGULO_ABIERTO = 90;

// =================================================
// TIEMPOS
// =================================================
const unsigned long TIEMPO_ABIERTO = 30000; // 30 segundos
unsigned long tiempoApertura = 0;
unsigned long tiempoMaximoApertura = 0;    // 30 + 30 s

// =================================================
// MAQUINA DE ESTADOS
// =================================================
enum Estado {
  ESPERANDO_PRESENCIA,
  TAPA_ABIERTA,
  ESPERANDO_CONFIRMACION_CIERRE
};

Estado estado = ESPERANDO_PRESENCIA;

// =================================================
// WIFI / MQTT
// =================================================
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
const char* DEVICE_ID = "esp_02";

WiFiClient espClient;
PubSubClient client(espClient);

enum BrokerSel { BROKER_NONE=-1, BROKER_FOG=0, BROKER_CLOUDLET=1 };
BrokerSel currentBroker = BROKER_NONE;

// Reintentos
unsigned long lastConnectAttemptMs = 0;
unsigned long connectIntervalMs    = 3000;   // backoff base
const unsigned long BACKOFF_MAX_MS = 30000;

// Reintentar volver al fog cada 2 minutos
const unsigned long PREFER_FOG_EVERY_MS = 120000; // 2 min
unsigned long lastFogTryMs = 0;

// ---------- WiFi ----------
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
  }
}

// ---------- Callback comandos ----------
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String t(topic);
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  // Aquí recibes comandos en NAPIoT/P3_02
  // Ejemplo simple: "ON" / "OFF"
  if (t == TOPIC_SUB) {
    Serial.print("CMD: ");
    Serial.println(msg);

    // TODO: activa/desactiva actuadores según msg
    // Si usas JSON, parsea aquí.
  }
}

String makeClientId() {
  uint64_t chipid = ESP.getEfuseMac();
  char buf[64];
  snprintf(buf, sizeof(buf), "esp_%s_%04X", DEVICE_ID, (uint16_t)(chipid & 0xFFFF));
  return String(buf);
}

// ---------- Conectar a broker específico ----------
bool connectTo(const char* host, uint16_t port, BrokerSel sel) {
  mqtt.setServer(host, port);
  mqtt.setSocketTimeout(3); // segundos
  mqtt.setKeepAlive(30);

  String cid = makeClientId();

  bool ok = false;
  if (MQTT_USER && MQTT_USER[0] != '\0') {
    ok = mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS);
  } else {
    ok = mqtt.connect(cid.c_str());
  }
  if (!ok) return false;

  currentBroker = sel;

  // Suscripción a comandos (NAPIoT/P3_02)
  mqtt.subscribe(TOPIC_SUB);

  return true;
}

// ---------- Failover: fog -> cloudlet ----------
bool connectFailover() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (mqtt.connected()) return true;

  // Cada cierto tiempo intenta volver a Fog
  bool forceFogTry = false;
  if (millis() - lastFogTryMs > PREFER_FOG_EVERY_MS) {
    lastFogTryMs = millis();
    forceFogTry = true;
  }

  // Orden de intentos:
  // - si toca volver al fog: fog primero
  // - si no: intenta el broker actual primero, luego el otro
  if (forceFogTry) {
    if (connectTo(FOG_HOST, FOG_PORT, BROKER_FOG)) return true;
    if (connectTo(CL_HOST,  CL_PORT,  BROKER_CLOUDLET)) return true;
    return false;
  }

  if (currentBroker == BROKER_CLOUDLET) {
    if (connectTo(CL_HOST,  CL_PORT,  BROKER_CLOUDLET)) return true;
    if (connectTo(FOG_HOST, FOG_PORT, BROKER_FOG)) return true;
  } else {
    // por defecto: fog primero
    if (connectTo(FOG_HOST, FOG_PORT, BROKER_FOG)) return true;
    if (connectTo(CL_HOST,  CL_PORT,  BROKER_CLOUDLET)) return true;
  }
  return false;
}

// ---------- Servicio MQTT en loop ----------
void mqttService() {
  ensureWiFi();

  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  unsigned long now = millis();
  if (now - lastConnectAttemptMs < connectIntervalMs) return;
  lastConnectAttemptMs = now;

  bool ok = connectFailover();
  if (ok) {
    connectIntervalMs = 3000; // reset backoff
  } else {
    connectIntervalMs = min(connectIntervalMs * 2UL, BACKOFF_MAX_MS);
  }
}

// ---------- Publicar datos ----------
bool publishTelemetry(const String& payload) {
  if (!mqtt.connected()) return false;
  return mqtt.publish(TOPIC_PUB, payload.c_str(), false);
}

// =================================================
// FUNCIONES DE CONTROL DE LA TAPA
// =================================================
void abrirTapa() {
  servoTapa.write(ANGULO_ABIERTO);

  // Marca de tiempo
  tiempoApertura = millis();
  tiempoMaximoApertura = tiempoApertura + (2 * TIEMPO_ABIERTO);

  estado = TAPA_ABIERTA;

  client.publish(TOPIC_STATE, "OPEN");
  Serial.println("Tapa ABIERTA");
}

void cerrarTapa() {
  servoTapa.write(ANGULO_CERRADO);
  estado = ESPERANDO_CONFIRMACION_CIERRE;

  client.publish(TOPIC_STATE, "CLOSE");
  Serial.println("Tapa CERRADA");
}

// =================================================
// CALLBACK MQTT (comandos manuales)
// =================================================
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

    if (act == "OPEN" && estado == ESPERANDO_PRESENCIA) {
  abrirTapa();
  }
    return true;
  }
}
}

// =================================================
// RECONEXION MQTT
// =================================================
/*void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_CONTENEDOR")) {
      client.subscribe(TOPIC_CMD);
    } else {
      delay(3000);
    }
  }
}*/

// =================================================
// SETUP
// =================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_IR, INPUT);
  pinMode(PIN_CRASH, INPUT);

  servoTapa.attach(PIN_SERVO, 500, 2400);
  servoTapa.write(ANGULO_CERRADO);

   mqtt.setCallback(onMqttMessage);

  ensureWiFi();
  connectFailover();

  Serial.println("Sistema listo");
}

// =================================================
// LOOP PRINCIPAL
// =================================================
void loop() {

  if (!client.connected()) reconnect();
  client.loop();

   mqttService();

  bool presencia = (digitalRead(PIN_IR) == LOW);
  bool tapaCerrada = (digitalRead(PIN_CRASH) == HIGH);
  unsigned long ahora = millis();

   static unsigned long lastSend = 0;
  unsigned long now = millis();
  if (now - lastSend > 2000) {
    lastSend = now;

StaticJsonDocument<256> doc;
  doc["presencia"] = presencia;
  doc["cierre_tapa"] = tapa_cerrada;

  char payload[256];
  serializeJson(doc, payload);
  publishTelemetry(payload);
  }


  switch (estado) {

    // ---------------------------
    case ESPERANDO_PRESENCIA:
      if (presencia) {
        abrirTapa();
      }
      break;

    // ---------------------------
    case TAPA_ABIERTA:

      // Cierre normal tras 30 s SIN presencia
      if (!presencia && (ahora - tiempoApertura >= TIEMPO_ABIERTO)) {
        cerrarTapa();
      }

      // Cierre forzado tras 60 s aunque haya presencia
      if (ahora >= tiempoMaximoApertura) {
        cerrarTapa();
      }

      break;

    // ---------------------------
    case ESPERANDO_CONFIRMACION_CIERRE:
      if (tapaCerrada) {
        client.publish(TOPIC_STATE, "CIERRE_OK"); // LED cierre
        estado = ESPERANDO_PRESENCIA;
      }
      break;
  }

  delay(100);
}

