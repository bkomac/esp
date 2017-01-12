#include <Arduino.h>

#include <FS.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

//needed for library
//#include <DNSServer.h>
#include <ESP8266WebServer.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#include <ArduinoJson.h>
#include <DHT.h>

ESP8266WebServer server(80);

WiFiClient client;
PubSubClient mqClient(client);

//Vcc measurement
//#define ADC_MODE(ADC_VCC);

//APP
String FIRM_VER = "1.3.2";
String SENSOR = "PIR"; //BMP180, HTU21, DHT11

String app_id;
float vcc;
String espIp;
String apSsid;
int rssi;
String ssid;


//CONF
char essid[40] = "";
char epwd[40] = "";

String MODE = "AUTO";
int timeOut = 5000;

//mqtt config
char mqttAddress[200] = "";
int mqttPort = 1883;
char mqttTopic[200] = "iot/sensor";

//REST API CONFIG
char rest_server[40] = "";

boolean rest_ssl = false;
char rest_path[200]  = "";
int rest_port = 80;
char api_token[100] = "";
char api_payload[400] = "";

boolean buttonPressed = false;
boolean requestSent = false;
int lastTime = millis();

//StaticJsonBuffer<500> jsonBuffer;

int BUILTINLED = 13;
int RELEY = 12;
int GPIO_IN = 14;
#define BUTTON 0

//DHT
#define DHTPIN 3
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

void setup() { //------------------------------------------------
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  pinMode(BUTTON, INPUT);
  pinMode(BUILTINLED, OUTPUT);

  digitalWrite(RELEY, LOW);
  digitalWrite(BUILTINLED, LOW);

  blink(1, 500);
  //dht.begin();

  app_id = "ESP" + getMac();
  Serial.print(F("**App ID: "));
  Serial.println(app_id);

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println(F("mounting FS..."));

  if (SPIFFS.begin()) {
    Serial.println(F("mounted file system"));
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println(F("reading config file"));
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println(F("opened config file"));
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& jsonConfig = jsonBuffer.parseObject(buf.get());
        jsonConfig.printTo(Serial);
        if (jsonConfig.success()) {
          Serial.println(F("\nparsed json"));

          //config parameters
        /*  String ssid1 = jsonConfig["ssid"].asString();
          ssid1.toCharArray(essid, 40, 0);
          String pwd1 = jsonConfig["password"].asString();
          pwd1.toCharArray(epwd, 40, 0);
          */

          String timeOut1 = jsonConfig["timeOut"];
          timeOut = timeOut1.toInt();
          String relley1 = jsonConfig["relleyPin"];
          RELEY = relley1.toInt();
          String gpioIn1 = jsonConfig["sensorInPin"];
          GPIO_IN = gpioIn1.toInt();
          String builtInLed1 = jsonConfig["statusLed"];
          BUILTINLED = builtInLed1.toInt();

          String mqttAddress1 = jsonConfig["mqttAddress"].asString();
          mqttAddress1.toCharArray(mqttAddress, 200, 0);
          String mqttPort1 = jsonConfig["mqttPort"];
          mqttPort = mqttPort1.toInt();
          String mqttTopic1 = jsonConfig["mqttTopic"].asString();
          mqttTopic1.toCharArray(mqttTopic, 200, 0);

          String rest_server1 = jsonConfig["restApiServer"].asString();
          rest_server1.toCharArray(rest_server, 40, 0);

          String rest_ssl1 = jsonConfig["restApiSSL"];
          if (rest_ssl1 == "true")
            rest_ssl = true;
          else
            rest_ssl = false;

          String rest_path1 = jsonConfig["restApiPath"].asString();
          rest_path1.toCharArray(rest_path, 200, 0);
          String rest_port1 = jsonConfig["restApiPort"];
          rest_port = rest_port1.toInt();
          String api_token1 = jsonConfig["restApiToken"].asString();
          api_token1.toCharArray(api_token, 200, 0);
          String api_payload1 = jsonConfig["restApiPayload"].asString();
          api_payload1.toCharArray(api_payload, 400, 0);

        } else {
          Serial.println(F("failed to load json config"));
        }
      }
    }
  } else {
    Serial.println(F("failed to mount FS"));
    blink(10, 50, 20);
  }
  //end read


  apSsid = "Config_" + app_id;

  pinMode(RELEY, OUTPUT);
  pinMode(BUILTINLED, OUTPUT);
  if (GPIO_IN < 100)
    pinMode(GPIO_IN, INPUT);


    if (String(essid) != "") {
      Serial.print("SID found. Trying to connect to ");
      Serial.print(essid);
      Serial.println("");
      WiFi.begin(essid, epwd);
      delay(100);
      if (testWifi()) {

      }else{
        setupAP();
      }
    }

    ssid = WiFi.SSID();
    Serial.print(F("\nconnected to "));
    Serial.print(ssid);
    Serial.print(F(" "));
    Serial.println(rssi);

    blink(3, 500);
  Serial.println(" ");
  IPAddress ip = WiFi.localIP();
  espIp = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
  Serial.print(F("local ip: "));
  Serial.println(espIp);
  yield();
  createWebServer();

  //MQTT
  if (mqttAddress != "") {
    mqClient.setServer(mqttAddress, mqttPort);
    mqClient.setCallback(mqCallback);
  }
}

//loop ----------------------------------------------------------
void loop() {
  delay(10);

  /*if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\nreconecting ..."));
    testWifi();
  }*/

  rssi = WiFi.RSSI();

  server.handleClient();


  int inputState = LOW;
  if (GPIO_IN < 100)
    inputState = digitalRead(GPIO_IN);

  int buttonState = HIGH;
  if (BUTTON < 100)
    buttonState = digitalRead(BUTTON);

vcc = ESP.getVcc() / 1000.00;
vcc = analogRead(A0);

delay(100);

  if (MODE == "AUTO") {
    if (inputState == HIGH ) {
      Serial.println(F("Sensor high..."));
      digitalWrite(RELEY, HIGH);
      digitalWrite(BUILTINLED, LOW);

      buttonPressed = false;
      String sensorData = "";
      if (!requestSent) {
        sendRequest(sensorData);
        requestSent = true;
        Serial.print(F("\nFree heap: "));
        Serial.println(ESP.getFreeHeap());
      }
      lastTime = millis();
    }
    if (millis() > lastTime + timeOut && !buttonPressed && inputState != HIGH) {
      digitalWrite(RELEY, LOW);
      digitalWrite(BUILTINLED, HIGH);
      requestSent = false;
    }
  }

  //button pressed
  if (buttonState == LOW) {
    Serial.println(F("Button pressed..."));
    buttonPressed = true;
    if (digitalRead(RELEY) == HIGH) {
      digitalWrite(RELEY, LOW);
      digitalWrite(BUILTINLED, HIGH);
    } else {
      digitalWrite(RELEY, HIGH);
      digitalWrite(BUILTINLED, LOW);
    }
    delay(300);
  }
  //  mqClient.subscribe(String(mqttTopic).c_str());
  //  mqPublish("Hi there!");
  delay(100);


}//---------------------------------------------------------------

//web server
void createWebServer()
{
  Serial.println(F("Starting server..."));
  Serial.println(F("REST handlers init..."));
  yield();
  server.on("/", []() {
    blink();
    String content;

    content = "<!DOCTYPE HTML>\r\n<html>";
    content += "<h1>ESP web server</h1>";
    content += "<p>IP: " + espIp + "</p>";
    content += "<p>MAC/AppId: " + app_id + "</p>";
    content += "<p>Version: " + FIRM_VER + "</p>";
    content += "<p>Voltage: " + String(vcc) + " V</p>";
    content += "</html>";
    server.send(200, "text/html", content);
  });

  server.on("/switch/auto", []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["rc"] = 0;
    if (digitalRead(RELEY) == HIGH) {
      root["status"] = "on";
      digitalWrite(BUILTINLED, LOW);
    } else {
      root["status"] = "off";
      digitalWrite(BUILTINLED, HIGH);
    }
    MODE = "AUTO";
    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
  });

  server.on("/switch/on", []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["rc"] = 0;
    root["status"] = "on";
    MODE = "MANUAL";
    String content;
    root.printTo(content);
    digitalWrite(RELEY, HIGH);
    digitalWrite(BUILTINLED, LOW);
    server.send(200, "application/json", content);
  });

  server.on("/switch/off", []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["rc"] = 0;
    root["status"] = "off";
    MODE = "MANUAL";
    String content;
    root.printTo(content);
    digitalWrite(RELEY, LOW);
    digitalWrite(BUILTINLED, HIGH);
    server.send(200, "application/json", content);
  });

  server.on("/switch/status", HTTP_GET, []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["rc"] = 0;
    root["mode"] = MODE;
    if (digitalRead(RELEY) == HIGH) {
      root["status"] = "on";
      digitalWrite(BUILTINLED, LOW);
    } else {
      root["status"] = "off";
      digitalWrite(BUILTINLED, HIGH);
    }
    JsonObject& meta = root.createNestedObject("meta");
    meta["version"] = FIRM_VER;
    meta["sensor"] = SENSOR;
    meta["voltage"] = vcc;

    meta["ssid"] = ssid;
    meta["rssi"] = rssi;
    meta["freeHeap"] = ESP.getFreeHeap();

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);

    Serial.print("/switch/status: ");
    root.printTo(Serial);

  });

  server.on("/update", HTTP_GET, []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["url"] = "Enter url to bin file here and POST json object to ESP.";

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
  });

  server.on("/update", HTTP_POST, []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(server.arg("plain"));
    Serial.println("Updating firmware...");
    String message = "";
    //    for (uint8_t i = 0; i < server.args(); i++) {
    //      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    //    }

    String url = root["url"];
    Serial.println("");
    Serial.print("Update url: ");
    Serial.println(url);

    blink(10, 80);
    String content;

    t_httpUpdate_return ret = ESPhttpUpdate.update(url);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        root["rc"] = ESPhttpUpdate.getLastError();
        message += "HTTP_UPDATE_FAILD Error (";
        message += ESPhttpUpdate.getLastError();
        message += "): ";
        message += ESPhttpUpdate.getLastErrorString().c_str();
        root["msg"] = message;
        root.printTo(content);
        server.send ( 200, "application/json", content );
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        root["rc"] = 1;
        root["msg"] = "HTTP_UPDATE_NO_UPDATES";
        root.printTo(content);
        server.send ( 200, "application/json", content );
        break;

      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        root["rc"] = 0;
        root["msg"] = "HTTP_UPDATE_OK";
        root.printTo(content);
        server.send ( 200, "application/json", content );
        break;
    }

  });

  server.on("/config", HTTP_GET, []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["timeOut"] = timeOut;
    root["relleyPin"] = RELEY;
    root["sensorInPin"] = GPIO_IN;
    root["statusLed"] = BUILTINLED;

    root["mqttAddress"] = mqttAddress;
    root["mqttPort"] = mqttPort;
    root["mqttTopic"] = mqttTopic;

    root["restApiServer"] = rest_server;
    root["restApiSSL"] = rest_ssl;
    root["restApiPath"] = rest_path;
    root["restApiPort"] = rest_port;
    root["restApiToken"] = api_token;
    root["restApiPayload"] = api_payload;

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
  });

  server.on("/config", HTTP_POST, []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(server.arg("plain"));
    Serial.println(F("\nSaving config..."));

    String timeOut1 = root["timeOut"];
    timeOut = timeOut1.toInt();
    String relley1 = root["relleyPin"];
    RELEY = relley1.toInt();
    String gpioIn1 = root["sensorInPin"];
    GPIO_IN = gpioIn1.toInt();
    String builtInLed1 = root["statusLed"];
    BUILTINLED = builtInLed1.toInt();

    String mqttAddress1 = root["mqttAddress"].asString();
    mqttAddress1.toCharArray(mqttAddress, 200, 0);
    String mqttPort1 = root["mqttPort"];
    mqttPort = mqttPort1.toInt();
    String mqttTopic1 = root["mqttTopic"].asString();
    mqttTopic1.toCharArray(mqttTopic, 200, 0);

    String rest_server1 = root["restApiServer"].asString();
    rest_server1.toCharArray(rest_server, 40, 0);

    String rest_ssl1 = root["restApiSSL"];
    if (rest_ssl1 == "true")
      rest_ssl = true;
    else
      rest_ssl = false;

    String rest_path1 = root["restApiPath"].asString();
    rest_path1.toCharArray(rest_path, 200, 0);
    String rest_port1 = root["restApiPort"];
    rest_port = rest_port1.toInt();
    String api_token1 = root["restApiToken"].asString();
    api_token1.toCharArray(api_token, 200, 0);
    String api_payload1 = root["restApiPayload"].asString();
    api_payload1.toCharArray(api_payload, 400, 0);

    root.printTo(Serial);

    saveConfig(root);
    pinMode(RELEY, OUTPUT);
    pinMode(GPIO_IN, INPUT);

    Serial.println(F("\nConfiguration is saved."));
    root["rc"] = 0;
    root["msg"] = "Configuration is saved.";

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);

    if (mqttAddress != "") {
      mqClient.setServer(mqttAddress, mqttPort);
    }
  });

  server.on("/ssid", HTTP_POST, []() {
    blink(5);
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(server.arg("plain"));
    Serial.print(F("\nSetting ssid to "));


    String ssid1 = root["ssid"].asString();
    ssid1.toCharArray(essid, 40, 0);
    String pwd1 = root["password"].asString();
    pwd1.toCharArray(epwd, 40, 0);

    Serial.println(essid);

    pinMode(RELEY, OUTPUT);
    pinMode(GPIO_IN, INPUT);

    Serial.println(F("\nNew ssid is set. ESP will reconect..."));
    root["rc"] = 0;
    root["msg"] = "New ssid is set. ESP will reconect.";

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);

    WiFi.disconnect();
    delay(1000);
    WiFi.begin(essid, epwd);
    delay(1000);
    testWifi();

  });

  server.on("/reset", HTTP_DELETE, []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["rc"] = 0;
    root["msg"] = "Reseting ESP config...";
    Serial.println(F("\nReseting ESP config..."));

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);
    delay(3000);

    //clean FS, for testing
    SPIFFS.format();
    delay(1000);
    //reset settings - for testing
    WiFi.disconnect(true);
    delay(1000);

    ESP.reset();
    delay(5000);
  });

  server.on("/reset", HTTP_GET, []() {
    blink();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["rc"] = 0;
    root["msg"] = "Reseting ESP...";
    Serial.println(F("\nReseting ESP..."));

    String content;
    root.printTo(content);
    server.send(200, "application/json", content);

    ESP.reset();
    delay(5000);
  });

  server.begin();
  Serial.println("Server started");

}//--

//send request ---------------------------------------------------------
void sendRequest(String sensorData)
{
  String api_payload_s = String(api_payload);
  if (api_payload_s != "")
    api_payload_s = api_payload_s + ", ";
  if (sensorData != "")
    sensorData = sensorData + ", ";
  String data = "{" + api_payload_s + "\"sensor\":{\"sensor_type\":\"" + SENSOR +
                "\", \"data\":{" + sensorData + "}" +
                ", \"ver\":\"" + FIRM_VER + "\"" +
                ", \"ip\":\"" + espIp + "\"" +
                ", \"id\":\"" + app_id + "\"" +
                ", \"vcc\":" + vcc +
                "}}";

  //REST request
  if (String(rest_server) != "") {
    WiFiClientSecure client;
    //WiFiClient client;
    int i = 0;
    String url = rest_path;
    Serial.println("");
    Serial.print(F("Connecting for request... "));
    Serial.print(String(rest_server));
    Serial.print(":");
    Serial.println(String(rest_port));

    while (!client.connect(rest_server, rest_port)) {
      Serial.print("Try ");
      Serial.print(i);
      Serial.print(" ... ");

      if (i == 4) {
        blink(5, 20);
        Serial.println("");
        return;
      }
      i++;
    }

    Serial.print("POST data to URL: ");
    Serial.println(url);
    delay(10);
    String req = String("POST ") + url + " HTTP/1.1\r\n" +
                 "Host: " + String(rest_server) + "\r\n" +
                 "User-Agent: ESP/1.0\r\n" +
                 "Content-Type: application/json\r\n" +
                 "Cache-Control: no-cache\r\n" +
                 "Sensor-Id: " + String(app_id) + "\r\n" +
                 "Token: " + String(api_token) + "\r\n" +
                 "Content-Type: application/x-www-form-urlencoded;\r\n" +
                 "Content-Length: " + data.length() + "\r\n" +
                 "\r\n" + data;
    Serial.print(F("Request: "));
    Serial.println(req);
    client.print(req);

    delay(100);

    Serial.println(F("Response: "));
    while (client.available()) {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }

    blink(2, 40);

    Serial.println(F("\nConnection closed"));
    req = "";
  }

  //MQTT publish
  if (String(mqttAddress) != "") {
    Serial.println();
    Serial.println(F("MQTT publish..."));

    mqPublish(data);
  }

  api_payload_s = "";
  data = "";


} //--

void saveConfig(JsonObject& json) {

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println(F("failed to open config file for writing"));
  }

  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
}

//MQTT
void mqCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print(F("Message arrived ["));
  Serial.print(topic);
  Serial.print(F("] "));
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  blink(3, 50);
}

boolean mqReconnect() {
  // Loop until we're reconnected
  while (!mqClient.connected()) {
    Serial.print(F("\nAttempting MQTT connection... "));
    Serial.print(mqttAddress);
    Serial.print(F(":"));
    Serial.print(mqttPort);
    Serial.print(F(" "));
    Serial.print(mqttTopic);

    // Attempt to connect
    if (mqClient.connect(app_id.c_str())) {
      Serial.print(F("\nconnected with cid: "));
      Serial.println(app_id);
      return true;
    } else {
      Serial.print(F("\nfailed to connect!"));
      return false;
    }
  }
  return true;
}

void mqPublish(String msg) {
  if (!mqClient.connected()) {
    mqReconnect();
  }
  mqClient.loop();

  Serial.print(F("Publish message to topic '"));
  Serial.print(mqttTopic);
  Serial.print(F("':"));
  Serial.println(msg);
  mqClient.publish(String(mqttTopic).c_str(), msg.c_str());

}

//tesing for wifi connection
bool testWifi() {
  int c = 0;
  Serial.println("Waiting for Wifi to connect...");
  while ( c < 10 ) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(F("WiFi connected to "));
      Serial.println(WiFi.SSID());

      blink(1, 250);
      return true;
    }
    blink(1, 200);
    delay(500);
    Serial.print(F("Retrying to connect to WiFi... "));
    Serial.println(WiFi.status());
    c++;
  }
  Serial.println(F(""));
  Serial.println(F("Connect timed out"));
  Serial.println(F("Resetnig ESP ..."));
  blink(20, 30);
  //delay(1000);
  //reset esp
  //ESP.reset();
  //delay(3000);
  return false;
}

void setupAP(void) {
  Serial.print("Setting up access point. WiFi.mode= ");
  Serial.println(WIFI_STA);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("Scanning network done");
  if (n == 0)
    Serial.println("No networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    Serial.println("---------------------------------");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
  String st = "<ol>";
  for (int i = 0; i < n; ++i)
  {
    // Print SSID and RSSI for each network found
    st += "<li>";
    st += WiFi.SSID(i);
    st += " (";
    st += WiFi.RSSI(i);
    st += ")";
    st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    st += "</li>";
  }
  st += "</ol>";
  delay(100);

  WiFi.softAP(apSsid.c_str());
  Serial.print("SoftAP ready. SID: ");
  Serial.print(ssid);

  Serial.println("Done.");
}


//blink
void blink (void) {
  blink(1, 30, 30);
}

void blink (int times) {
  blink(times, 40, 40);
}

void blink (int times, int milisec) {
  blink(times, milisec, milisec);
}
void blink (int times, int himilisec, int lowmilisec) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUILTINLED, LOW);
    delay(lowmilisec);
    digitalWrite(BUILTINLED, HIGH);
    delay(himilisec);
  }
}

String getMac() {
  unsigned char mac[6];
  WiFi.macAddress(mac);
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
  }
  return result;
}
