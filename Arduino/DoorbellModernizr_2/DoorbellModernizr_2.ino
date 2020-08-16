/***************************************************************************
 Code to use with MQTT wifi doorbell, see https://www.tindie.com/products/ErikLemcke/mqtt--wifi-doorbell-with-esp8266/
 Created by Erik Lemcke 12-05-2020


 This code can be used from the arduino library, you will need the following libraries:
 https://github.com/tzapu/WiFiManager     Included in this repository (src folder)
 https://github.com/bblanchon/ArduinoJson
 https://github.com/knolleary/pubsubclient
 https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS

 credits:

 https://tzapu.com/                   for the awesome WiFiManger library
 https://github.com/esp8266/Arduino   fopr the ESP8266 arduino core
 https://arduinojson.org/             for the ArduinoJson library
 https://pubsubclient.knolleary.net/  for the mqtt library
  
 ***************************************************************************/
#include <fs.h>                   //this needs to be first, or it all crashes and burns...
#import "index.h"

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "src/WiFiManager.h" 

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#include <base64.h>
#include <ESP8266HTTPClient.h>

int doorbellState = 0;
int resetState = 0;
const int doorbellPin = 14;

WiFiClient espClient;
HTTPClient http; 
PubSubClient client(espClient);

WiFiManager wifiManager;
ESP8266WebServer server(80);

unsigned long previousMillis = 0;       

DNSServer dnsServer; //Needed for captive portal when device is already connected to a wifi network

//extra parameters
char mqtt_server[40];
char mqtt_port[6] ;
char mqtt_username[40];
char mqtt_password[40];
char mqtt_topic[40];
char mqtt_switch_topic[47];
char mqtt_status_topic[47];
char mqtt_status[60] = "unknown";

char dz_idx[5];
char oh_itemid[40];

//flag for saving data
bool shouldSaveConfig = false;
bool apstarted = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Connection to previous set wifi failed. Erasing settings before becoming AP");
  //SPIFFS.format(); 
}


//Handle webserver root request
void handleRoot() {
  Serial.println("Config page is requested");
  String addy = server.client().remoteIP().toString();
//  Serial.println(addy);
  
  //determine if this user is connected to the AP or comming from the network the device is connected to
  //when connected to AP, configuration is not allowed
  if (addy == "192.168.4.2"){
      server.send(200, "text/html", "The doorbell modernizr can be configured on address http:// " + WiFi.localIP().toString() + " when connected to wifi network " + WiFi.SSID());
  } else {
    String configPage = FPSTR(config_page);
    configPage.replace("{v}", "Doorbell modernizr configuration");
    configPage.replace("{1}", mqtt_server);
    configPage.replace("{2}", mqtt_port);
    configPage.replace("{3}", mqtt_username);
    configPage.replace("{4}", mqtt_password);
    configPage.replace("{5}", mqtt_topic);
    configPage.replace("{6}", mqtt_status);
    configPage.replace("{7}", dz_idx);
    configPage.replace("{8}", oh_itemid);
    
    server.send(200, "text/html", configPage);
  }
}

void saveSettings() {
  Serial.println("Handling webserver request savesettings");

  //store the updates values in the json config file
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = server.arg("mqtt_server");
    json["mqtt_port"] = server.arg("mqtt_port");
    json["mqtt_username"] = server.arg("mqtt_username");
    json["mqtt_password"] = server.arg("mqtt_password");
    json["mqtt_topic"] = server.arg("mqtt_topic");

    json["dz_idx"] = server.arg("dz_idx");
    json["oh_itemid"] = server.arg("oh_itemid");
   
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();

    //put updated parameters into memory so they become effective immediately
    server.arg("mqtt_server").toCharArray(mqtt_server,40);
    server.arg("mqtt_port").toCharArray(mqtt_port,40);
    server.arg("mqtt_username").toCharArray(mqtt_username,40);
    server.arg("mqtt_password").toCharArray(mqtt_password,40);
    server.arg("mqtt_topic").toCharArray(mqtt_topic,40);
    server.arg("dz_idx").toCharArray(dz_idx,40);
    server.arg("oh_itemid").toCharArray(oh_itemid,40);
   
    server.send(200, "text/html", "Settings have been saved. You will be redirected to the configuration page in 5 seconds <meta http-equiv=\"refresh\" content=\"5; url=/\" />");
    
    //mqtt settings might have changed, let's reconnect to the mqtt server if one is configured
    if (strlen(mqtt_topic) != 0){
      Serial.println("mqtt topic set, need to connect");
      reconnect();
    }
}

void handleOn(){
  digitalWrite(13, LOW);
  digitalWrite(2, LOW);
  Serial.println("turning doorbell on");
  if(client.connected()){
     client.publish(mqtt_status_topic, "ON" , true);
  }
  server.send(200);
}

void handleOff(){
  digitalWrite(13, HIGH);
  digitalWrite(2, HIGH);
  Serial.println("turning doorbell off"); 
  if(client.connected()){
     client.publish(mqtt_status_topic, "OFF" , true);
  }
  server.send(200);
}


void handleIncommingMqttMessages(char* topic, byte* payload, unsigned int length) {
  strcpy(mqtt_status_topic, mqtt_topic);
  strcat(mqtt_status_topic, "_status");    
  
  String Message = "";
  
  for (int i = 0; i < length; i++) {
    Message += (char)payload[i];
  }

  if (Message == "ON"){
    handleOn();
  }

  if (Message == "OFF"){
    handleOff();
  }
}

void setup() {
 
  Serial.begin(115200);
  Serial.println();
  Serial.println("Doorbell modernizr 2");
  Serial.println("Firmware version 1.0");

  pinMode(doorbellPin, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(13, OUTPUT); //relay
  pinMode(2, OUTPUT); //led
 
  //read configuration from FS json
  Serial.println("mounting file system");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (json.success()) {
          Serial.println("parsed config file json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);

          strcpy(dz_idx, json["dz_idx"]);
          strcpy(oh_itemid, json["oh_itemid"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount file system");
  }
  //end read

  WiFiManagerParameter custom_mqtt_server("server", "ip address", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_username("username", "username", mqtt_username, 40);
  WiFiManagerParameter custom_mqtt_password("password", "password", mqtt_password, 40);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 40);

  WiFiManagerParameter custom_dz_idx("dzidx", "Domoticz idx", dz_idx, 5);
  WiFiManagerParameter custom_oh_itemid("ohitemid", "OpenHAB itenId", oh_itemid, 40);

  WiFiManagerParameter custom_text("<p>Fill the folowing values with your home assistant/ domoticz infromation. Username and password are optional</p>");
  wifiManager.addParameter(&custom_text);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.setAPCallback(configModeCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);

  WiFiManagerParameter custom_text1("<p>Fill the folowing field with your MQTT topic for home assistant</p>");
  wifiManager.addParameter(&custom_text1);
  
  wifiManager.addParameter(&custom_mqtt_topic);

  WiFiManagerParameter custom_text2("<p>Fill the folowing field with your IDX value for Domoticz</p>");
  wifiManager.addParameter(&custom_text2);
    
  wifiManager.addParameter(&custom_dz_idx);

  WiFiManagerParameter custom_text3("<p>Fill the folowing field with your itemId for OpenHAB</p>");
  wifiManager.addParameter(&custom_text3);

 wifiManager.addParameter(&custom_oh_itemid);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration  
    
  if (!wifiManager.autoConnect("Doorbell modernizr")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected to wifi network");

  //Define url's for webserver 
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/saveSettings", saveSettings);
  server.on("/", handleRoot);
  server.onNotFound([]() {
    handleRoot();
  });
  server.begin();

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());

  strcpy(dz_idx, custom_dz_idx.getValue());
  strcpy(oh_itemid, custom_oh_itemid.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_topic"] = mqtt_topic;
    json["dz_idx"] = dz_idx;
    json["oh_itemid"] = oh_itemid;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  //MQTT
  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setCallback(handleIncommingMqttMessages);

  Serial.println("Doorbell modernizr ip on " + WiFi.SSID() + ": " + WiFi.localIP().toString());
  client.publish(mqtt_status_topic, "ON" , true);
 
}


//MQTT reconnect function
void reconnect() {
  client.disconnect();
  client.setServer(mqtt_server, atoi(mqtt_port));
  Serial.print("Attempting MQTT connection to ");
  Serial.print(mqtt_server);
  Serial.print(" on port ");
  Serial.print(mqtt_port);
  Serial.print("...");
  
  if (client.connect("DoorbellModernizr2x", mqtt_username, mqtt_password)) {
     Serial.println("connected");
     String("<div style=\"color:green;float:left\">connected</div>").toCharArray(mqtt_status,60);
     Serial.print("sending 'off' message to ");
     Serial.print(mqtt_server);
     Serial.print(" on port ");
     Serial.print(mqtt_port);
     Serial.print(" with topic ");
     Serial.println(mqtt_topic);
     client.publish(mqtt_topic, "off" , true);

     strcpy(mqtt_switch_topic, mqtt_topic);
     strcat(mqtt_switch_topic, "_switch");    
     client.subscribe(mqtt_switch_topic);
   } else {
     Serial.print("failed, rc=");
     String("<div style=\"color:red;float:left\">connection failed</div>").toCharArray(mqtt_status,60);
     Serial.print(client.state());
     Serial.println(" try again in 5 seconds");

  

    unsigned long previousMillis1 = 0;

     //connection failed, Go into a (non-blocking) loop until we are connected 
     while (!client.connected()) {
        resetstate();
         server.handleClient();  //call to handle webserver connection needed, because the while loop will block the processor
         unsigned long currentMillis1 = millis();
         if(currentMillis1 - previousMillis1 >= 5000) {
         previousMillis1 = currentMillis1;
         Serial.print("Attempting MQTT connection...");
          if (client.connect("Doorbellmodernizr2", mqtt_username, mqtt_password)) {
                Serial.println("connected");
                String("<div style=\"color:green;float:left\">connected</div>").toCharArray(mqtt_status,60);
             } else {
                     String("<div style=\"color:red;float:left\">connection failed</div>").toCharArray(mqtt_status,60);
                Serial.print("failed, rc=");
                Serial.print(client.state());
                Serial.println(" try again in 5 seconds");
             }
         }
      }
   }
}

long lastMsg = 0;

//Initialize a reset if pin 12 is low
void resetstate (){
   resetState = digitalRead(12);
   if (resetState == LOW){
    Serial.println("It seems someone wants to go for a reset...");
  
    int count = 0;

    //Flash the led for 5 seconds
    while (count < 25 && digitalRead(12) == LOW) {
      digitalWrite(LED_BUILTIN, HIGH);   
      delay(100);                      
      digitalWrite(LED_BUILTIN, LOW);    
      delay(100);                      
      count ++;
    }
  
    resetState = digitalRead(12);
    //See if they still want to go for it
    if (resetState == LOW){
       Serial.println("Let's do it");
       SPIFFS.format();
       wifiManager.resetSettings();
       delay(500);
       ESP.restart();  
    } else {
      Serial.println("They chickened out...");
      Serial.println("Starting AP that can be used to obtain IP");

      boolean result = false;
      while (result == false){
        result = WiFi.softAP("Doorbell modernizr online" );  
      }
      
      if(result == true)
      {
        dnsServer.start(53, "*", WiFi.softAPIP());
          
        apstarted = true;
        previousMillis =  millis();
        Serial.println("AP IP address: " +  WiFi.softAPIP().toString());
      }
    }
  }
}


void loop() {

  //if a AP is started, kill it after 3 minutes
  if (apstarted == true){
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= 300000) {
      previousMillis = currentMillis;
      Serial.println("Stopping the AP, 3 minutes are past!");
      WiFi.softAPdisconnect(false);
      apstarted = false;    
    }
  }
 
  resetstate();
  
  if (strlen(mqtt_topic) != 0){
     //try to reconnect to mqtt server if connection is lost
    if (!client.connected()) { 
      reconnect();
    }
    client.loop();
  }
  
  server.handleClient();
  dnsServer.processNextRequest();
   
  doorbellState = digitalRead(doorbellPin);
  resetState = digitalRead(12);
    
 if ( doorbellState == LOW ) {
  
    if (strlen(mqtt_topic) != 0){
      client.publish(mqtt_topic, "on" , true);
      Serial.print("Doorbell is pressed!, sending 'on' message to ");
      Serial.print(mqtt_server);
      Serial.print(" on port ");
      Serial.print(mqtt_port);
      Serial.print(" with topic ");
      Serial.println(mqtt_topic);
      
  
      //wait 5 seconds, then publish the off message
      delay( 5000 );
  
      
      Serial.print("sending 'off' message to ");
      Serial.print(mqtt_server);
      Serial.print(" on port ");
      Serial.print(mqtt_port);
      Serial.print(" with topic ");
      Serial.println(mqtt_topic);
      client.publish(mqtt_topic, "off" , true);    
    }
   if (strlen(dz_idx) != 0){
    if (espClient.connect(mqtt_server,atoi(mqtt_port))){
      Serial.println("sending 'on' message to Domiticz");
      espClient.print("GET /json.htm?type=command&param=udevice&idx=");
      espClient.print(String(dz_idx));
      espClient.print("&nvalue=1");
      
      if (strlen(mqtt_username) != 0){
        espClient.print("&username=");
        espClient.print(base64::encode(mqtt_username));
        espClient.print("&password=");
        espClient.print(base64::encode(mqtt_password));
      }
      
      espClient.println(" HTTP/1.1");
      espClient.print("Host: ");
      espClient.print(String(mqtt_server));
      espClient.print(":");
      espClient.println(String(mqtt_port));
      espClient.println("User-Agent: doorbell-modernizr");
      espClient.println("Connection: close");
      espClient.println();
      espClient.stop();
   } else {
     Serial.println("connect failed");
   }
      //wait 5 seconds, then publish the off message
      delay( 5000 );
    if (espClient.connect(mqtt_server,atoi(mqtt_port))){
        Serial.println("sending 'off' message to Domiticz");
        espClient.print("GET /json.htm?type=command&param=udevice&idx=");
        espClient.print(String(dz_idx));
        espClient.print("&nvalue=0");
  
         if (strlen(mqtt_username) != 0){
          espClient.print("&username=");
          espClient.print(base64::encode(mqtt_username));
          espClient.print("&password=");
          espClient.print(base64::encode(mqtt_password));
        }
        
        espClient.println(" HTTP/1.1");
        espClient.print("Host: ");
        espClient.print(String(mqtt_server));
        espClient.print(":");
        espClient.println(String(mqtt_port));
        espClient.println("User-Agent: doorbell-modernizr");
        espClient.println("Connection: close");
        espClient.println();
        espClient.stop();
     } else {
       Serial.println("connect failed");
     } 
   }

   // OpenHAB
   if (strlen(oh_itemid) != 0){
      Serial.println("sending 'ON' message to openHAB");
      http.begin("http://" + String(mqtt_server) + ":" + String(mqtt_port) +"/rest/items/" + String(oh_itemid)); 
      http.POST("ON");
      http.end();

      delay(5000);

      Serial.println("sending 'OFF' message to openHAB");
      http.begin("http://" + String(mqtt_server) + ":" + String(mqtt_port) +"/rest/items/" + String(oh_itemid)); 
      http.POST("OFF"); 
      http.end();
    
   }  
  }
}
