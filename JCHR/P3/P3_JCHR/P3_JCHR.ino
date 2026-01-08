#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT11.h>
#include <ArduinoJson.h>

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
const char* ssid = "NAPIoT";
const char* password = "Uvigo1_2025";
const char* mqttServer = "192.168.1.74";
const int mqttPort = 1883;
const char* mqttUser = "";
const char* mqttPassword = "";

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

 WiFi.begin(ssid, password);
  Serial.println("...................................");
  Serial.print("Conectando á WiFi.");

while (WiFi.status() != WL_CONNECTED)

  { delay(500);
  Serial.print(".") ;
  }
  Serial.println();
  Serial.println("Connectado á rede WiFi!");
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);


while (!client.connected())

  { Serial.println("Conectando ao broker MQTT...");
// IMPORTANTE: O ID de cliente da seguinte línea (NodoNAPIoT) debe ser “único”!!

if (client.connect("JCHR", mqttUser, mqttPassword )) // JGA id client único para el broker mqtt
  {
    Serial.println("Conectado ao broker MQTT!");
   // Subscripción ao topic
    client.subscribe("NAPIoT/P3_02"); //Gestión de los actuadores desde node-RED mediante el topic NAPIoT/P3_02
    Serial.println("Subscrito ao topic NAPIoT/P3_02");
  }

else
  { Serial.print("Erro ao conectar co broker: ");
  Serial.print(client.state());
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
             "{\"temp_in\":%d,\"hum_in\":%d}", temp_in, hum_in);
    client.publish("NAPIoT/P3_01", payload_in);
  }

  if (hum_out >= 0 && hum_out <= 100 && temp_out >= -20 && temp_out <= 100) {
    char payload_out[64];
    snprintf(payload_out, sizeof(payload_out),
             "{\"temp_out\":%d,\"hum_out\":%d}", temp_out, hum_out);
    client.publish("NAPIoT/P3_01", payload_out);
  }

  delay(1000);
}

// Función de callback que procesa as mensaxes MQTT recibidas
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaxe recibida[");
  Serial.print(topic);
  Serial.print("] ");

  // Imprimese o payload da mensaxe
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.println("JSON inválido");
    return;
  }

  bool rele_ventilador = doc["rele_ventilador"]; //Obtenemos el valor de rele_ventilador del JSON (true/false)
 
  if (rele_ventilador) {
    Serial.println("Ventilador ON");
    digitalWrite(RELAY_PIN, HIGH); // Activa el relé
  } else {
    digitalWrite(RELAY_PIN, LOW); // Desactiva el relé
    Serial.println("Ventilador OFF");
  }

}

// Reconecta co broker MQTT se se perde a conexión
void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conectar a broker MQTT...");
    
    // Inténtase conectar indicando o ID do dispositivo
    //IMPORTANTE: este ID debe ser único!
    if (client.connect("JCHR")) {
      Serial.println("conectado!");
      
      // Subscripción ao topic
      client.subscribe("NAPIoT/P3_02");
      Serial.println("Subscrito ao topic");
    } else {
      Serial.print("erro na conexión, erro=");
      Serial.print(client.state());
      Serial.println(" probando de novo en 5 segundos");
      delay(5000);
    }
  }
}
