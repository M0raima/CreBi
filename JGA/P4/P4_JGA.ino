#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>


int ledPin3W = 11;
int pulsador = 18;

//matriz 8x8
// =====================
static const int PIN_DIN = 23;
static const int PIN_CLK = 18;
static const int PIN_CS  = 5;

int Power = 0;
int distancia = 0;
bool estadoBoton =  false; 
// Matriz 8x8
static const int PIN_DIN = 23;
static const int PIN_CLK = 18;
static const int PIN_CS  = 5;

// acelerómetro
static const int PIN_ACC_INT = 34; 

void IRAM_ATTR isrAcc() {
  acc_evento = true;
}

void acelerometro_int_init() {
  pinMode(PIN_ACC_INT, INPUT); 
  attachInterrupt(digitalPinToInterrupt(PIN_ACC_INT), isrAcc, RISING);
}

volatile bool acc_evento = false;

static void max7219Send(uint8_t reg, uint8_t data) {
  digitalWrite(PIN_CS, LOW);
  for (int i = 7; i >= 0; --i) { // reg
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_DIN, (reg >> i) & 0x01);
    digitalWrite(PIN_CLK, HIGH);
  }
  for (int i = 7; i >= 0; --i) { // data
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_DIN, (data >> i) & 0x01);
    digitalWrite(PIN_CLK, HIGH);
  }
  digitalWrite(PIN_CS, HIGH);
}

void matriz8x8_init(uint8_t intensidad /*0..15*/) {
  pinMode(PIN_DIN, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);

  max7219Send(0x0F, 0x00);                 // test off
  max7219Send(0x0C, 0x01);                 // normal
  max7219Send(0x0B, 0x07);                 // scan 8 filas
  max7219Send(0x09, 0x00);                 // no decode
  max7219Send(0x0A, intensidad & 0x0F);    // brillo

  for (uint8_t row = 1; row <= 8; row++) max7219Send(row, 0x00);
}

void matriz8x8_setRow(uint8_t row /*0..7*/, uint8_t bits) {
  if (row > 7) return;
  max7219Send(row + 1, bits);
}

void matriz8x8_draw(const uint8_t frame[8]) {
  for (uint8_t r = 0; r < 8; r++) matriz8x8_setRow(r, frame[r]);
}

// Dibuja "nivel" llenando filas desde abajo: 0..8 filas
void matriz8x8_drawLevel(uint8_t filledRows) {
  if (filledRows > 8) filledRows = 8;
  uint8_t frame[8];

  // r=0 es fila superior, r=7 inferior
  for (uint8_t r = 0; r < 8; r++) {
    bool filled = (r >= (8 - filledRows)); // llena desde abajo
    frame[r] = filled ? 0xFF : 0x00;       // 0xFF = 8 LEDs encendidos
  }
  matriz8x8_draw(frame);
}


// Ultrasonidos
// =====================
static const int PIN_TRIG = 26;
static const int PIN_ECHO = 27;

// Devuelve distancia en cm (o -1 si fallo)
float ultrasonidos_cm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  // timeout ~ 30ms => ~5m (sobrado). Ajusta si quieres.
  unsigned long dur = pulseIn(PIN_ECHO, HIGH, 30000UL);
  if (dur == 0) return -1.0f;

  // Velocidad del sonido aprox: 343 m/s => 29.1 us por cm ida
  // Distancia = (duración_us / 2) / 29.1
  float cm = (dur * 0.0343f) / 2.0f;
  return cm;
}

// Mapear distancia a filas (cerca = más filas, lejos = menos filas)
uint8_t distancia_a_filas(float cm, float cmMin, float cmMax) {
  if (cm < 0) return 0;

  if (cm <= cmMin) return 8;
  if (cm >= cmMax) return 0;

  // Invertido: cmMin -> 8, cmMax -> 0
  float t = (cm - cmMin) / (cmMax - cmMin); // 0..1
  float val = 8.0f * (1.0f - t);            // 8..0
  uint8_t filas = (uint8_t)(val + 0.5f);    // redondeo
  if (filas > 8) filas = 8;
  return filas;
}

// =====================
// Relleno
// =====================
uint8_t filasActual = 0;

void setup() {
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  matriz8x8_init(8);
  matriz8x8_drawLevel(0);
}

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
const char* DEVICE_ID = "esp_03";

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

void setup()
{
  Serial.begin(9600);

  mqtt.setCallback(onMqttMessage);

  ensureWiFi();
  connectFailover();

  //Salidas
  pinMode(PinTxUS, OUTPUT); 
  //Entradas
  pinMode(pulsador, INPUT); 

  acelerometro_int_init();

}

// Devuelve true una sola vez cuando se detecta evento (movimiento)
bool acelerometro_activado() {
  if (acc_evento) {
    acc_evento = false;
    return true;
  }
  return false;
}
int desplazamiento = 0;

void loop()
{
 // Verifica se o cliente está conectado
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (acelerometro_activado()) {
    desplazamiento++;
  }

  //Ultrasonidos
  nivel_residuos = ultrasonidos_cm();
  //Serial.print("Distancia: ");
  //Serial.println(distancia); //nivel de llenado del CREBI

  const float cmMin = 5.0f;
  const float cmMax = 60.0f;

  float cm = nivel_residuos;
  uint8_t filasObjetivo = distancia_a_filas(cm, cmMin, cmMax);

  // Suavizado: se mueve de 1 fila por ciclo para que "vaya rellenando"
  if (filasObjetivo > filasActual) filasActual++;
  else if (filasObjetivo < filasActual) filasActual--;

  matriz8x8_drawLevel(filasActual);

  delay(80); // velocidad del "rellenado"

  //Valores de prueba para Edge Computing
  if (nivel_residuos > 50) led_almacenamiento = 1;

  int desplazamiento = 1;

  char payload[64];
  snprintf(payload, sizeof(payload),
             "{\"nivel_residuos\":%d,\"desplazamiento\":%d, ,\"led_almacenamiento\":%d}",
             nivel_residuos, desplazamiento, led_almacenamiento);

   publishTelemetry(payload);
   delay(5000);

}


// Función de callback que procesa as mensaxes MQTT recibidas
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

  
  if (strcmp(act, "alarma_matriz") == 0) {
    if (act == "true") {
      digitalWrite(ledPin3W, HIGH);
    }
    if (act == "false") {
      digitalWrite(ledPin3W, LOW);
    }
    return true;
  }
}
}


}

