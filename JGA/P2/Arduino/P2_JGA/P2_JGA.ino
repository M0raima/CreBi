#include <WiFi.h>
#include <PubSubClient.h>

int PinRxUS = 8; //define ultrasonic signal receiver pin ECHO
int PinTxUS = 10; //define ultrasonic signal transmitter pin TRIG
int ledPin3W = 11;
int pulsador = 18;
int Power = 0;
int distancia = 0;
bool estadoBoton =  false; 

const char* ssid = "OPPO"; // Substituír polo SSID da nosa rede WiFi
const char* password = "12345678"; // Substituír polo password da nosa rede WiFi
const char* mqttServer = "test.mosquitto.org";
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
if (client.connect("jga_vig_iot", mqttUser, mqttPassword ))
  {
    Serial.println("Conectado ao broker MQTT!");
   // Subscripción ao topic
    client.subscribe("jga_vig_iot_tx");
    Serial.println("Subscrito ao topic");
  }

else
  { Serial.print("Erro ao conectar co broker: ");
  Serial.print(client.state());
  delay(2000);
    }
  }

  //Salidas
  pinMode(ledPin3W, OUTPUT); 
  pinMode(PinTxUS, OUTPUT); 
  //Entradas
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


  /*estadoBoton = digitalRead(pulsador);

  if (estadoBoton == HIGH){
    Power = LOW;
  }else{
    Power = HIGH;
  }

  digitalWrite(ledPin3W, Power);*/
  

  //Ultrasonidos
  distancia = medirDistancia();
  //Serial.print("Distancia: ");
  //Serial.println(distancia); //nivel de llenado del CREBI

  //Valor provisional de prueba para FlowFuse
  int pin3W = 25;

  char payload[64];
  snprintf(payload, sizeof(payload),
             "{\"distancia\":%d,\"pin3W\":%d}",
             distancia, pin3W);

    client.publish("jga_vig_iot_rx", payload);
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

  // Controlamos o estado do LED en fucnión da mensaxe
  if (message == "0") {
    digitalWrite(ledPin3W, LOW); // Apaga o LED
    Serial.println("LED OFF");
  } else if (message == "1") {
    digitalWrite(ledPin3W, HIGH); // Acende o LED
    Serial.println("LED ON");
  } else {
    Serial.println("Comando descoñecido");
  }
}

// Reconecta co broker MQTT se se perde a conexión
void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conectar a broker MQTT...");
    
    // Inténtase conectar indicando o ID do dispositivo
    //IMPORTANTE: este ID debe ser único!
    if (client.connect("jga_vig_iot")) {
      Serial.println("conectado!");
      
      // Subscripción ao topic
      client.subscribe("jga_vig_iot_tx");
      Serial.println("Subscrito ao topic");
    } else {
      Serial.print("erro na conexión, erro=");
      Serial.print(client.state());
      Serial.println(" probando de novo en 5 segundos");
      delay(5000);
    }
  }
}

