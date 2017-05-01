/*
  Basic sketch to detect a button press and send it to io.adafruit.com.

  This is listening for a button press/release on D4 (GPIO 2).
  Once a change in state is detected, it will send an MQTT message to io.adafruit.com
  to record the new value.

  This also has a simple webserver in it to serve up a page that allows the 
  internal LED to be toggled on/off.
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include <Adafruit_NeoPixel.h>

/************************* WiFi Access Point *********************************/

#define WLAN_SSID       "****"
#define WLAN_PASS       "****"

/************************* Adafruit.io Setup *********************************/

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883                   // use 8883 for SSL
#define AIO_USERNAME    "****"
#define AIO_KEY         "****"

/******************************* LED PIN definition **************************/

#define LED_PIN D2
#define CONNECT_TIMEOUT 86400000L

/************ Global State (you don't need to change this!) ******************/

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

ESP8266WebServer server(80);
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
/****************************** Feeds ***************************************/

// Setup a feed called 'photocell' for publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
//Adafruit_MQTT_Publish garage1 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/garage1");

// Setup a feed called 'garage1' for subscribing to changes.
Adafruit_MQTT_Subscribe garage1 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/garage1");
Adafruit_MQTT_Subscribe garage2 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/garage2");

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, LED_PIN);
int internalLEDPin;
int internalLEDValue;
unsigned long lastPingTime;
unsigned long pingTimeoutPeriod;
unsigned long lastConnectTry;
boolean bGarage1Closed;
boolean bGarage2Closed;
boolean bStatusChange;
boolean connectionFail;

void handleRoot()
{
  String message = "Garage Door Display!! Use ";
  message += "/upload to update the flash with new code. Use the .bin file created by the IDE. Use /status to see current status.";
  server.send(200, "text/plain", message);
}

void handleUpload()
{
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", serverIndex);
}

void handleUpdate()
{
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
  ESP.restart();
}

void handleUpdate2()
{
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    Serial.setDebugOutput(true);
    WiFiUDP::stopAll();
    Serial.printf("Update: %s\n", upload.filename.c_str());
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if(!Update.begin(maxSketchSpace)){//start with max available size
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_END){
    if(Update.end(true)){ //true to set the size to the current progress
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
    Serial.setDebugOutput(false);
  }
  yield();
}

void handleStatus()
{
  for (uint8_t i=0; i<server.args(); i++)
  {
    if (server.argName(i) == "internal")
    {
      if (server.arg(i) == "off")
      {
        internalLEDValue = HIGH;
      }
      else if (server.arg(i) == "on")
      {
        internalLEDValue = LOW;
      }
      digitalWrite(internalLEDPin, internalLEDValue);
    }
  }
  
  String message = "<html>\n<head>";
  message += "<style>\n";
  message += "table {\n";
  message += "  border-collapse : collapse;\n";
  message += "}\n";
  message += "table, th, td {border : 1px solid black;}\n";
  message += "th {background-color : lightblue;}\n";
  message += "td {text-align : center;}\n";
  message += "</style>\n";
  message += "</head><body>";
  message += "LED directory\n\n";
  message += "<table><tr><th>LED</th><th>State</th></tr>";
  message += "<tr><td>Internal</td><td><a href=\"/LED?internal=";
  message += internalLEDValue == HIGH ? "on" : "off";
  message += "\">";
  message += internalLEDValue == HIGH ? "Off" : "On";
  message += "</a></td></tr>";
  message += "<tr><td>Garage Door 1</td><td>";
  message += bGarage1Closed == false ? "Open" : "Closed";
  message += "</td></tr>";
  message += "<tr><td>Garage Door 2</td><td>";
  message += bGarage2Closed == false ? "Open" : "Closed";
  message += "</td></tr>";
  message += "</table>";
  message += "<br>";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  message += "</body>";
  message += "</html>";

  for (uint8_t i=0; i<server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(200, "text/html", message);
}

void handleNotFound() 
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i=0; i<server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);

}

// the setup function runs once when you press reset or power the board
void setup() {
  boolean connectedToWiFi = false;
  connectionFail = false;
  // initialize digital pin LED_BUILTIN as an output.
  Serial.begin(115200);
  delay(10);
  pixels.begin();
  pinMode(internalLEDPin, OUTPUT);
  internalLEDValue = LOW;

  // Turn on the Blue LED
  digitalWrite(internalLEDPin, internalLEDValue);
  lastPingTime = 0;
  lastConnectTry = 0L;
  pingTimeoutPeriod = MQTT_CONN_KEEPALIVE * 1000 - 20000;

  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  // Turn on white light to show we are trying to connect to the WiFi.
  pixels.setPixelColor(0, pixels.Color(255, 255, 255));  // White color
  pixels.show();
 
  WiFi.begin(WLAN_SSID, WLAN_PASS);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    connectedToWiFi = !connectedToWiFi;
    if (connectedToWiFi)
      pixels.setPixelColor(0, pixels.Color(0,0,0));
    else
      pixels.setPixelColor(0, pixels.Color(255,255,255));
    Serial.print(".");
    pixels.show();
  }
  connectedToWiFi = true;
  Serial.println("");
  Serial.println("WiFi connected");
 
  // Start the server
  server.begin();
  Serial.println("Server started");
 
  // Print the IP address
  Serial.print("Use this URL : ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  server.on("/", handleRoot);
  server.on("/inline", []()
  {
    server.send(200, "text/plain", "this works as well");
  });
  server.on("/status", handleStatus);
  server.on("/update", HTTP_POST, handleUpdate, handleUpdate2);
  server.on("/upload", handleUpload);
  
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

//  Serial.println("");
//  Serial.print("Value = ");
//  Serial.println(value);
  bGarage1Closed = true;
  bGarage2Closed = true;
  bStatusChange = false;

  // Now subscribe to the MQTT feed
  if (!mqtt.subscribe(&garage1)) {
    pixels.setPixelColor(0, pixels.Color(0,255,255));
    pixels.show();
    while(1);
  }
  if (!mqtt.subscribe(&garage2)) {
    pixels.setPixelColor(0, pixels.Color(0,255,255));
    pixels.show();
    while(1);
  }

  pixels.setPixelColor(0, pixels.Color(255, 255, 255));  // White color
  pixels.show();
}

// the loop function runs over and over again forever
void loop() {
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  if (MQTT_connect()) {
    Adafruit_MQTT_Subscribe *subscription;
    while ((subscription = mqtt.readSubscription(5000))) {
      if (subscription == &garage1) {
        Serial.print(F("Garage 1 Got: "));
        Serial.println((char *)garage1.lastread);
        if (strcmp((char *)garage1.lastread, "1") == 0) {
          bGarage1Closed = true;
        } else {
          bGarage1Closed = false;
        }
        // We'll assume a status changed since we got a read on the subscription
        bStatusChange = true;
      }
      
      if (subscription == &garage2) {
        Serial.print(F("Garage 2 Got: "));
        Serial.println((char *)garage2.lastread);
        if (strcmp((char*)garage2.lastread, "1") == 0) {
          bGarage2Closed = true;
        } else {
          bGarage2Closed = false;
        }
        // We'll assume a status changed since we got a read on the subscription
        bStatusChange = true;
      }
    }
  
    if ((millis() - lastPingTime) > pingTimeoutPeriod) {
      Serial.println("Pinging MQTT server to keep ourselves alive");
      // ping the server to keep the mqtt connection alive
      // NOT required if you are publishing once every KEEPALIVE seconds
      if(! mqtt.ping()) {
        mqtt.disconnect();
        Serial.println("Disconnected from MQTT server due to failure of ping");
        pixels.setPixelColor(0, pixels.Color(170, 0, 255));    // Purple color - means no connectivity
        pixels.show();
      } else {
        // Only change the ping time if we had a sucessful ping.
        lastPingTime = millis();
        bStatusChange = true;
      }
    }
  }

  if (bStatusChange) {
    bStatusChange = false;
    if (bGarage1Closed && bGarage2Closed) {
      digitalWrite(internalLEDPin, LOW);  // Turns on LED
      pixels.setPixelColor(0, pixels.Color(0, 255, 0));  // Green color
    } else {
      digitalWrite(internalLEDPin, HIGH); // Turns off LED
      if (!bGarage1Closed && !bGarage2Closed)
        pixels.setPixelColor(0, pixels.Color(255, 0, 0));  // Red color
      else if (bGarage1Closed && !bGarage2Closed)
        pixels.setPixelColor(0, pixels.Color(255, 100, 0)); // Orange Color
      else if (!bGarage1Closed && bGarage2Closed)
        pixels.setPixelColor(0, pixels.Color(255, 255, 0)); // Yellow Color
    }
    pixels.show();
  }

  server.handleClient();
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
boolean MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return true;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         pixels.setPixelColor(0, pixels.Color(0, 0, 255));
         pixels.show();
         return false;
       }
  }
  Serial.println("MQTT Connected!");
  bStatusChange = true;
  return true;
}
