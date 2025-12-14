#include <WiFi.h>
#include <PubSubClient.h>

// ===== CONFIGURACIÓN PINES =====
const int RxPin = 5;   // Entrada Rx IR
const int TxPin = 4;   // Salida controlada por MQTT

// ===== CONFIGURACIÓN WIFI =====
const char* ssid = "iPhonePao";         
const char* password = "wifipao73"; 

// ===== CONFIGURACIÓN MQTT =====
const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;
const char* mqtt_topic_rx = "Paola3050";   // Publicar eventos Rx IR
const char* mqtt_topic_tx = "Paola3051";   // Recibir control GPIO 4

WiFiClient espClient;
PubSubClient client(espClient);

// ===== VARIABLES DE DEBOUNCE =====
int estadoConfirmado = HIGH;        
unsigned long tiempoUltimoCambio = 0;
const unsigned long debounceLowMs = 30;   
const unsigned long debounceHighMs = 30;  

// ===== PUBLICACIÓN PERIÓDICA =====
const unsigned long pubIntervalMs = 1000; // Publicar estado cada 1s
unsigned long lastPubTime = 0;

// ===== FUNCIONES =====
void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi conectada!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  Serial.print("Mensaje recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  if (message == "1") {
    digitalWrite(TxPin, HIGH);
    Serial.println("Presencia detectada");
  } else if (message == "0") {
    digitalWrite(TxPin, LOW);
    Serial.println("AreaCReBI-UnderControl");
  } else {
    Serial.println("Comando desconocido");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conectar a broker MQTT...");
    if (client.connect("ESP32_Client")) {
      Serial.println("conectado!");
      client.subscribe(mqtt_topic_tx);
    } else {
      Serial.print("Error estado=");
      Serial.print(client.state());
      Serial.println(" reintentando en 5s");
      delay(5000);
    }
  }
}

void setup() {
  pinMode(RxPin, INPUT_PULLUP); // Sensor IR con pull-up
  pinMode(TxPin, OUTPUT);
  digitalWrite(TxPin, LOW);

  Serial.begin(115200);
  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// ===== LOOP =====
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long tiempoActual = millis();
  int lectura = digitalRead(RxPin); // 0 o 1 según sensor

  // ===== NEGADOR AUTOMÁTICO =====
  int valorNegado = 1 - lectura; // Esto asegura que Flowfuse reciba el valor negado

  // ===== DETECCIÓN DE FLANCOS =====
  // Flanco descendente (1 → 0)
  if (estadoConfirmado == HIGH && lectura == LOW) {
    if (tiempoActual - tiempoUltimoCambio >= debounceLowMs) {
      estadoConfirmado = LOW;
      tiempoUltimoCambio = tiempoActual;
      Serial.println("INTRUDER-AreaCReBI");

      // Publicar inmediatamente valor negado
      long ts = millis();
      char msg[50];
      snprintf(msg, sizeof(msg), "{\"value\":%d,\"ts\":%lu}", valorNegado, ts);
      client.publish(mqtt_topic_rx, msg);
    }
  }

  // Flanco ascendente (0 → 1)
  if (estadoConfirmado == LOW && lectura == HIGH) {
    if (tiempoActual - tiempoUltimoCambio >= debounceHighMs) {
      estadoConfirmado = HIGH;
      tiempoUltimoCambio = tiempoActual;
      Serial.println("AreaCReBI-UnderControl");

      // Publicar inmediatamente valor negado
      long ts = millis();
      char msg[50];
      snprintf(msg, sizeof(msg), "{\"value\":%d,\"ts\":%lu}", valorNegado, ts);
      client.publish(mqtt_topic_rx, msg);
    }
  }

  // ===== PUBLICACIÓN PERIÓDICA =====
  if (tiempoActual - lastPubTime >= pubIntervalMs) {
    lastPubTime = tiempoActual;
    long ts = millis();
    char msg[50];
    snprintf(msg, sizeof(msg), "{\"value\":%d,\"ts\":%lu}", valorNegado, ts);
    client.publish(mqtt_topic_rx, msg);

  }
}