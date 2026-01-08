#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ---------- Pines ----------
#define PIN_IR        8
#define PIN_SERVO     10
#define PIN_CRASH     6

// ---------- Servo ----------
Servo servoTapa;
const int ANGULO_CERRADO = 0;
const int ANGULO_ABIERTO = 90;

// ---------- Tiempos ----------
const unsigned long TIEMPO_MIN_ABIERTO = 30000; // 30 s
const unsigned long TIEMPO_MAX_ABIERTO = 60000; // 60 s

unsigned long tiempoApertura = 0;

// ---------- Estados ----------
enum Estado {
  ESPERANDO_PRESENCIA,
  TAPA_ABIERTA,
  ESPERANDO_CONFIRMACION_CIERRE
};

Estado estado = ESPERANDO_PRESENCIA;

const char* ssid = "NAPIoT"; // Substituír polo SSID da nosa rede WiFi
const char* password = "Uvigo1_2025"; // Substituír polo password da nosa rede WiFi
const char* mqttServer = "192.168.1.74";
const int mqttPort = 1883;
const char* mqttUser = "";
const char* mqttPassword = "";

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);

  pinMode(PIN_IR, INPUT);
  pinMode(PIN_CRASH, INPUT);

  servoTapa.setPeriodHertz(50);
  servoTapa.attach(PIN_SERVO, 500, 2400);

  servoTapa.write(ANGULO_CERRADO);

  Serial.println("Contenedor listo");

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

if (client.connect("PLB", mqttUser, mqttPassword )) // JGA id client único para el broker mqtt
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

void loop() {

  // Verifica se o cliente está conectado
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  bool presencia = (digitalRead(PIN_IR) == LOW);
  bool tapaCerrada = (digitalRead(PIN_CRASH) == HIGH);
  unsigned long ahora = millis();

  switch (estado) {

    // ---------- 1. Espera presencia ----------
    case ESPERANDO_PRESENCIA:
      if (presencia) {
        servoTapa.write(ANGULO_ABIERTO);
        tiempoApertura = ahora;
        estado = TAPA_ABIERTA;

        Serial.println("Presencia detectada → Tapa abierta");

        int mqtt_presencia = 1;
        int cierre_tapa = 1;

        char payload[64];
        snprintf(payload, sizeof(payload),
             "{\"presencia\":%d,\"cierre_tapa\":%d}",
             mqtt_presencia, cierre_tapa);

    client.publish("NPAIoT/P3_01", payload); // Envío de los datos recogidos por los sensores a Node-RED mediante el topic NPAIoT/P3_01
    delay(5000);

      }
      break;

    // ---------- 2. Tapa abierta ----------
    case TAPA_ABIERTA: {
      unsigned long tiempoAbierto = ahora - tiempoApertura;

      // Cierre por tiempo máximo
      if (tiempoAbierto >= TIEMPO_MAX_ABIERTO) {
        cerrarTapa();
      }
      // Cierre normal: mínimo cumplido y sin presencia
      else if (!presencia && tiempoAbierto >= TIEMPO_MIN_ABIERTO) {
        cerrarTapa();
      }
      break;
    }

    // ---------- 3. Espera confirmación de cierre ----------
    case ESPERANDO_CONFIRMACION_CIERRE:
      if (tapaCerrada) {
        estado = ESPERANDO_PRESENCIA;
        Serial.println("Tapa cerrada confirmada → Ciclo reiniciado");
      }
      break;
  }
}

// ---------- Función de cierre ----------
void cerrarTapa() {
  servoTapa.write(ANGULO_CERRADO);
  estado = ESPERANDO_CONFIRMACION_CIERRE;
  Serial.println("Cerrando tapa...");
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

  bool servo = doc["servo"]; //Obtenemos el valor de servo del JSON (true/false)
  

  if (servo) {
    Serial.println("Activación del servomotor");
    servoTapa.write(ANGULO_ABIERTO);
  } else {
    servoTapa.write(ANGULO_CERRADO);
    Serial.println("Desactivación del servomotor");
  }

}

// Reconecta co broker MQTT se se perde a conexión
void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conectar a broker MQTT...");
    
    // Inténtase conectar indicando o ID do dispositivo
    //IMPORTANTE: este ID debe ser único!
    if (client.connect("PLB")) {
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
