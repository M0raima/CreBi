#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>


int PinRxUS = 8; //define ultrasonic signal receiver pin ECHO
int PinTxUS = 10; //define ultrasonic signal transmitter pin TRIG
int ledPin3W = 11;
int pulsador = 18;
int Power = 0;
int distancia = 0;
bool estadoBoton =  false; 

const char* ssid = "NAPIoT"; // Substituír polo SSID da nosa rede WiFi
const char* password = "Uvigo1_2025"; // Substituír polo password da nosa rede WiFi
const char* mqttServer = "192.168.1.74";
const int mqttPort = 1883;
const char* mqttUser = "";
const char* mqttPassword = "";

WiFiClient espClient;
PubSubClient client(espClient);

void setup()
{
  Serial.begin(9600);

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

if (client.connect("JGA", mqttUser, mqttPassword )) // JGA id client único para el broker mqtt
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

  //Salidas provisionales
  pinMode(ledPin3W, OUTPUT); 
  pinMode(PinTxUS, OUTPUT); 

  //Entradas provisionales
  pinMode(pulsador, INPUT); 
  pinMode(PinRxUS, INPUT); 
}
void loop()
{
 // Verifica se o cliente está conectado
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  

  //Ultrasonidos
  nivel_residuos = medirDistancia();
  //Serial.print("Distancia: ");
  //Serial.println(distancia); //nivel de llenado del CREBI

  //Valores de prueba para Edge Computing
  int led_almacenamiento = 1;
  int desplazamiento = 1;

  char payload[64];
  snprintf(payload, sizeof(payload),
             "{\"nivel_residuos\":%d,\"desplazamiento\":%d}",
             nivel_residuos, desplazamiento);

    client.publish("NPAIoT/P3_01", payload); // Envío de los datos recogidos por los sensores a Node-RED mediante el topic NPAIoT/P3_01
    delay(5000);

}

//Sensor de ultrasonidos para medir el nivel de llenado del CReBI
int medirDistancia(){

  digitalWrite(PinTxUS, LOW);
  delayMicroseconds(2);
  digitalWrite(PinTxUS, HIGH); // Pulse for 10μ s to trigger   ultrasonic detection
  delayMicroseconds(10);
  digitalWrite(PinTxUS, LOW);
  int distance = pulseIn(PinRxUS, HIGH); // Read receiver pulse time
  distance= distance/58; // Transform pulse time to distance
  //Serial.print("Distancia: ");
  //Serial.println(distance); //Output distance
  delay(50);
  return distance;
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

  bool alarma_matriz = doc["alarma_matriz"]; //Obtenemos el valor de alarma_matriz del JSON (true/false)
  bool led_almacenamiento = doc["led_almacenamiento"]; //Obtenemos el valor de led_almacenamiento del JSON (true/false)

  if (led_almacenamiento) {
    Serial.println("Tiempo de almacenamiento máximo expirado");
    digitalWrite(ledPin3W, HIGH); // Acende o LED
  } else {
    digitalWrite(ledPin3W, LOW); // Apaga o LED
    Serial.println("Motor OFF");
  }

}

// Reconecta co broker MQTT se se perde a conexión
void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conectar a broker MQTT...");
    
    // Inténtase conectar indicando o ID do dispositivo
    //IMPORTANTE: este ID debe ser único!
    if (client.connect("JGA")) {
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

