#include <WiFi.h>
#include <PubSubClient.h>
// Include the DHT11 library for interfacing with the sensor.
#include <DHT11.h>

// Create an instance of the DHT11 class.
// - For Arduino: Connect the sensor to Digital I/O Pin 2.
// - For ESP32: Connect the sensor to pin GPIO2 or P2.
// - For ESP8266: Connect the sensor to GPIO2 or D4.
DHT11 dht11(38);

const char* ssid = "Galaxy A50"; // Substituír polo SSID da nosa rede WiFi
const char* password = "casa8080**"; // Substituír polo password da nosa rede WiFi
const char* mqttServer = "test.mosquitto.org";
const int mqttPort = 1883;
const char* mqttUser = "";
const char* mqttPassword = "";

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("...................................");
  Serial.print("Conectando á WiFi.");

while (WiFi.status() != WL_CONNECTED)

  { delay(500);
  Serial.print(".") ;
  }
  Serial.println("Connectado á rede WiFi!");
  client.setServer(mqttServer, mqttPort);

while (!client.connected())

  { Serial.println("Conectando ao broker MQTT...");
// IMPORTANTE: O ID de cliente da seguinte línea (NodoNAPIoT) debe ser “único”!!
if (client.connect("jc_iot_2025", mqttUser, mqttPassword ))
  Serial.println("Conectado ao broker MQTT!");

else
  { Serial.print("Erro ao conectar co broker: ");
  Serial.print(client.state());
  delay(2000);
    }
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  client.loop();

  /***************************/
  // Attempt to read the temperature and humidity values from the DHT11 sensor.
    int temperature = dht11.readTemperature();

    int temp_out = 0;
    int hum_out = 0;
    // If using ESP32 or ESP8266 (xtensa architecture), uncomment the delay below.
    // This ensures stable readings when calling methods consecutively.
    // delay(50);

    int humidity = dht11.readHumidity();

    // Check the results of the readings.
    // If there are no errors, print the temperature and humidity values.
    // If there are errors, print the appropriate error messages.
    if (temperature != DHT11::ERROR_CHECKSUM && temperature != DHT11::ERROR_TIMEOUT &&
        humidity != DHT11::ERROR_CHECKSUM && humidity != DHT11::ERROR_TIMEOUT)
    {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °C");

        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
        
    }
    else
    {
        if (temperature == DHT11::ERROR_TIMEOUT || temperature == DHT11::ERROR_CHECKSUM)
        {
            Serial.print("Temperature Reading Error: ");
            Serial.println(DHT11::getErrorString(temperature));
        }
        if (humidity == DHT11::ERROR_TIMEOUT || humidity == DHT11::ERROR_CHECKSUM)
        {
            Serial.print("Humidity Reading Error: ");
            Serial.println(DHT11::getErrorString(humidity));
        }
    }

    // Wait for 1 seconds before the next reading.
    delay(1000);

  /*********/
  /* json*/
    char payload[64];
   /* if ((humidity <= 100) && (temperature <= 100)) 

    temp_out = temperature + 5;
    hum_out = humidity + 20;

    {
    snprintf(payload, sizeof(payload),"{\"temp_in\":%d,\"hum_in\":%d,\"temp_out\":%d,\"hum_out\":%d }", temperature, humidity, temp_out, hum_out);
    client.publish("jc_iot_2025", payload);
    }
    */
///////////////////////////////////////////////////////////////
if ((humidity >= 0 && humidity <= 100) && (temperature >= -20 && temperature <= 100)) {
    
    temp_out = temperature + 5;
    hum_out = humidity + 20;

    snprintf(payload, sizeof(payload),
             "{\"temp_in\":%d,\"hum_in\":%d,\"temp_out\":%d,\"hum_out\":%d}",
             temperature, humidity, temp_out, hum_out);

    client.publish("jc_iot_2025", payload);
}
//////////////////////////////////////////////////////////////

  /*********/
  /***************************/
  //char str[16];

  //sprintf(str, "%u", random(100)); //Con esto simulamos a xeración do valor dun sensor
  //client.publish("jc_iot_2025", str); // Usar o mesmo topic que en Node-RED
  //Serial.println(str);
/*
    itoa(temperature, str, 10);
    //client.publish("jc_iot_2025", str); // Usar o mesmo topic que en Node-RED
    Serial.println(str);
    
   if (humidity <= 100)
{
    itoa(humidity, str, 10);
    //client.publish("jc_iot_2025", str); // Usar o mesmo topic que en Node-RED
    Serial.println(str);
}
*/

  delay(5000);

}
