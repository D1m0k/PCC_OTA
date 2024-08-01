#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>
#ifdef ESP32
  #include <Update.h>
  #include <HTTPUpdate.h>
#else
  #include <ESP8266HTTPUpdate.h>
#endif


// Пины по умолчанию
#define DEFAULT_ONE_WIRE_BUS 4 // Пин по умолчанию для подключения датчиков

// // Объявление объектов
// OneWire oneWire(oneWireBusPin);
// DallasTemperature sensors(&oneWire);
// WiFiClient espClient;
// PubSubClient client(espClient);
// AsyncWebServer server(80);
// DNSServer dns;

const char* configFile = "/config.json";

// Уникальный идентификатор устройства
String deviceID = String((uint32_t)ESP.getEfuseMac(), HEX);

// Структура конфигурации для кнопок
struct ButtonConfig {
  String name;
  int pin;
  unsigned long duration;
  String topic;
  int mode;
};

// Структура основной конфигурации
struct Config {
  String ssid;
  String password;
  String mqtt_server;
  String mqtt_user;
  String mqtt_password;
  String sensor1_name;
  String sensor2_name;
  int oneWireBus_pin;
  std::vector<ButtonConfig> buttons;
};

// Config config = {
//   "", "", "", "", "",
//   "CPU", "CHIPSET",
//   oneWireBusPin
//   // {{"Reset", 25, 1000, "esp32/reset", OUTPUT}, {"Power", 26, 1000, "esp32/power", OUTPUT}}
// };

Config config;
AsyncWebServer server(80);
DNSServer dns;
WiFiClient espClient;
PubSubClient client(espClient);
OneWire *oneWire;
DallasTemperature *sensors;

// Функция загрузки конфигурации
void loadConfig() {
  if (LittleFS.begin()) {
    if (LittleFS.exists(configFile)) {
      File file = LittleFS.open(configFile, "r");
      if (file) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        if (!error) {
          config.ssid = doc["ssid"].as<String>();
          config.password = doc["password"].as<String>();
          config.mqtt_server = doc["mqtt_server"].as<String>();
          config.mqtt_user = doc["mqtt_user"].as<String>();
          config.mqtt_password = doc["mqtt_password"].as<String>();
          config.sensor1_name = doc["sensor1_name"].as<String>();
          config.sensor2_name = doc["sensor2_name"].as<String>();
          config.oneWireBus_pin = doc["oneWireBus_pin"].as<int>()  | DEFAULT_ONE_WIRE_BUS;;
          // oneWireBusPin = config.oneWireBus_pin;

          JsonArray buttons = doc["buttons"].as<JsonArray>();
          for (JsonObject btn : buttons) {
            ButtonConfig button;
            button.name = btn["name"].as<String>();
            button.pin = btn["pin"].as<int>();
            button.duration = btn["duration"].as<unsigned long>();
            button.topic = btn["topic"].as<String>();
            button.mode = btn["mode"].as<int>();
            config.buttons.push_back(button);
            pinMode(button.pin, button.mode);
          }
          Serial.println("Config loaded successfully.");
        } else {
          Serial.println("Failed to deserialize config file.");
        }
        file.close();
      } else {
        Serial.println("Failed to open config file.");
      }
    } else {
      Serial.println("Config file not found.");
    }
    LittleFS.end();
  } else {
    Serial.println("Failed to mount file system.");
  }
}

// Функция сохранения конфигурации
void saveConfig() {
  if (LittleFS.begin()) {
    File file = LittleFS.open(configFile, "w");
    if (file) {
      JsonDocument doc;
      doc["ssid"] = config.ssid;
      doc["password"] = config.password;
      doc["mqtt_server"] = config.mqtt_server;
      doc["mqtt_user"] = config.mqtt_user;
      doc["mqtt_password"] = config.mqtt_password;
      doc["sensor1_name"] = config.sensor1_name;
      doc["sensor2_name"] = config.sensor2_name;
      doc["oneWireBus_pin"] = config.oneWireBus_pin;
      
      JsonArray buttons = doc["buttons"].to<JsonArray>();
      for (ButtonConfig button : config.buttons) {
        JsonObject btn = buttons.add<JsonObject>();
        btn["name"] = button.name;
        btn["pin"] = button.pin;
        btn["duration"] = button.duration;
        btn["topic"] = button.topic;
        btn["mode"] = button.mode;
      }
      
      if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write to config file.");
      } else {
        Serial.println("Config saved successfully.");
      }
      file.close();
    } else {
      Serial.println("Failed to open config file for writing.");
    }
    LittleFS.end();
  } else {
    Serial.println("Failed to mount file system.");
  }
}

void handleSaveConfig(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true)) config.ssid = request->getParam("ssid", true)->value();
  if (request->hasParam("password", true)) config.password = request->getParam("password", true)->value();
  if (request->hasParam("mqtt_server", true)) config.mqtt_server = request->getParam("mqtt_server", true)->value();
  if (request->hasParam("mqtt_user", true)) config.mqtt_user = request->getParam("mqtt_user", true)->value();
  if (request->hasParam("mqtt_password", true)) config.mqtt_password = request->getParam("mqtt_password", true)->value();
  if (request->hasParam("sensor1_name", true)) config.sensor1_name = request->getParam("sensor1_name", true)->value();
  if (request->hasParam("sensor2_name", true)) config.sensor2_name = request->getParam("sensor2_name", true)->value();
  if (request->hasParam("oneWireBus_pin", true)) {
    config.oneWireBus_pin = request->getParam("oneWireBus_pin", true)->value().toInt();
  }
  
  config.buttons.clear();
  int i = 0;
  while (true) {
    String btnName = "button_name" + String(i);
    String btnPin = "button_pin" + String(i);
    String btnDuration = "button_duration" + String(i);
    String btnTopic = "button_topic" + String(i);
    String btnMode = "button_mode" + String(i);

    if (request->hasParam(btnName, true) && request->hasParam(btnPin, true) &&
        request->hasParam(btnDuration, true) && request->hasParam(btnTopic, true) && request->hasParam(btnMode, true)) {
      ButtonConfig button;
      button.name = request->getParam(btnName, true)->value();
      button.pin = request->getParam(btnPin, true)->value().toInt();
      button.duration = request->getParam(btnDuration, true)->value().toInt();
      button.topic = request->getParam(btnTopic, true)->value();
      button.mode = request->getParam(btnMode, true)->value().toInt();
      config.buttons.push_back(button);
      pinMode(button.pin, button.mode);
      i++;
    } else {
      break;
    }
  }

  saveConfig();
  request->send(200, "text/plain", "Config saved");
  // request->redirect("/");
}

// AJAX обработчик для удаления кнопок
void handleDeleteButton(AsyncWebServerRequest *request) {
  Serial.println("Deleting button...");
  if (request->hasParam("index", true)) {
    int index = request->getParam("index", true)->value().toInt();
    if (index >= 0 && index < config.buttons.size()) {
      config.buttons.erase(config.buttons.begin() + index);
      saveConfig();
      request->send(200, "text/plain", "Button deleted");
    } else {
      request->send(400, "text/plain", "Invalid index");
    }
  } else {
    request->send(400, "text/plain", "Index not provided");
  }
}

void handleRestart(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Restarting...");
  delay(1000);
  ESP.restart();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  for (ButtonConfig button : config.buttons) {
    if (String(topic) == button.topic) {
      digitalWrite(button.pin, HIGH);
      delay(button.duration);
      digitalWrite(button.pin, LOW);
      Serial.print("Button ");
      Serial.print(button.name);
      Serial.print(" triggered for ");
      Serial.print(button.duration);
      Serial.println(" ms");
    }
  }
}

void handleButtonState(AsyncWebServerRequest *request) {
  int j = 0;
  String json = "[";
  for (size_t i = 0; i < config.buttons.size(); i++) {
    ButtonConfig button = config.buttons[i];
    if (button.mode == INPUT || button.mode == INPUT_PULLUP || button.mode == INPUT_PULLDOWN) {
      if (j > 0) json += ",";
      json += "{\"pin\":" + String(button.pin) + ",\"state\":" + String(!digitalRead(button.pin)) + ",\"id\":" + String(i) + "}";
      j++;
    }
  }
  json += "]";
  request->send(200, "application/json", json);
}

void handleTemperature(AsyncWebServerRequest *request) {
  sensors->requestTemperatures();
  float temp1 = sensors->getTempCByIndex(0);
  float temp2 = sensors->getTempCByIndex(1);
  String json = "{\"temp1\":" + String(temp1) + ",\"temp2\":" + String(temp2) + "}";
  request->send(200, "application/json", json);
}

void handleUpdate(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError()) ? "Update Failed" : "Update Success");
  response->addHeader("Connection", "close");
  request->send(response);
  delay(1000);
  ESP.restart();
}

void handleUpdateUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    Serial.printf("Update: %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { // start with max available size
      Update.printError(Serial);
    }
  }
  if (Update.write(data, len) != len) {
    Update.printError(Serial);
  }
  if (final) {
    if (Update.end(true)) { // true to set the size to the current progress
      Serial.printf("Update Success: %u\nRebooting...\n", index + len);
    } else {
      Update.printError(Serial);
    }
  }
}

void handleUpdateUrl(AsyncWebServerRequest *request) {
  if (request->hasParam("url", true)) {
    String url = request->getParam("url", true)->value();
    WiFiClient client;

#ifdef ESP32
    HTTPClient http;
    http.begin(client, url);  // Use WiFiClientSecure for HTTPS
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient * stream = http.getStreamPtr();
      size_t total = http.getSize();
      size_t written = 0;
      if (Update.begin(total)) {
        written = Update.writeStream(*stream);
        if (written == total) {
          if (Update.end(true)) {
            Serial.println("Update Success: Rebooting...");
            request->send(200, "text/plain", "Update Success: Rebooting...");
            delay(1000);
            ESP.restart();
          } else {
            Serial.printf("Update failed. Error #: %u\n", Update.getError());
            request->send(500, "text/plain", "Update failed");
          }
        } else {
          Serial.printf("Written only : %u/%u bytes\n", written, total);
          request->send(500, "text/plain", "Update failed");
        }
      } else {
        Serial.printf("Not enough space to begin OTA\n");
        request->send(500, "text/plain", "Not enough space");
      }
    } else {
      Serial.printf("HTTP GET failed. Error #: %d\n", httpCode);
      request->send(500, "text/plain", "HTTP GET failed");
    }
    http.end();

#else
    HTTPUpdateResult ret = ESPhttpUpdate.update(client, url);
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        request->send(500, "text/plain", "HTTP_UPDATE_FAILED");
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        request->send(200, "text/plain", "HTTP_UPDATE_NO_UPDATES");
        break;
      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        request->send(200, "text/plain", "Update Success: Rebooting...");
        delay(1000);
        ESP.restart();
        break;
    }
#endif
  } else {
    request->send(400, "text/plain", "Invalid request");
  }
}

void handleOTA(AsyncWebServerRequest *request) {
  String html = "<html><body>";
  html += "<h1>OTA Update</h1>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='update'>";
  html += "<input type='submit' value='Upload'>";
  html += "</form>";
  html += "<form method='POST' action='/update_url'>";
  html += "<input type='text' name='url' placeholder='Firmware URL'>";
  html += "<input type='submit' value='Update'>";
  html += "</form>";
  html += "<button onclick=\"location.href='/'\">BACK</button><br>";
  html += "</body></html>";
  request->send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }

  loadConfig();

  oneWire = new OneWire(config.oneWireBus_pin);
  sensors = new DallasTemperature(oneWire);
  sensors->begin();
  // Настройка WiFi
  Serial.println("Setting up WiFi...");
  AsyncWiFiManager wifiManager(&server, &dns);
  if (config.ssid.isEmpty()) {
    Serial.println("Starting WiFi AP for configuration...");
    wifiManager.autoConnect(deviceID.c_str());
  } else {
    WiFi.begin(config.ssid.c_str(), config.password.c_str(), 4);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("WiFi connection failed. Starting AP for configuration...");
      wifiManager.autoConnect(deviceID.c_str());
    } else {
      Serial.print("Connected to ");
      Serial.println(config.ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    }
  }

  // Настройка MQTT
  Serial.println("Setting up MQTT...");
  client.setServer(config.mqtt_server.c_str(), 1883);
  client.setCallback(callback);

  // Настройка веб-сервера
  Serial.println("Setting up web server...");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><head><link rel='icon' href='data:;base64,iVBORw0KGgo='></head><body>";
    html += "<h1>Device Info</h1>";
    html += "<p>Device ID: " + deviceID + "</p>";
    html += "<p>Sensor 1 (" + config.sensor1_name + "): <span id=\"temp1\"></span> &deg;C</p>";
    html += "<p>Sensor 2 (" + config.sensor2_name + "): <span id=\"temp2\"></span> &deg;C</p>";
    for (size_t i = 0; i < config.buttons.size(); i++) {
      ButtonConfig button = config.buttons[i];
      if (button.mode == OUTPUT) {
        html += "<button onclick=\"location.href='/trigger?pin=" + String(button.pin) + "&duration=" + String(button.duration) + "'\">" + button.name + "</button><br>";
      } else {
        html += "<p>" + button.name + ": <span id=\"button" + String(i) + "\">" + (!digitalRead(button.pin) ? "HIGH" : "LOW") + "</span></p><br>";
      }
    }
    html += "<form action=\"/config\" method=\"GET\"><button type=\"submit\">Config</button></form>";
    html += "<form action=\"/restart\" method=\"POST\"><button type=\"submit\">Restart ESP</button></form>";
    html += "<script>";
    html += "function updateButtonStates() {";
    html += "fetch('/button_state').then(response => response.json()).then(data => {";
    // html += "console.log(data);";
    html += "data.forEach((button, index) => {";
    html += "pin = `button${button.id}`;";
    // html += "console.log(pin);";
    html += "document.getElementById(pin).innerText = button.state ? 'HIGH' : 'LOW';";
    // html += "document.getElementById(`button${index}`).innerText = button.state ? 'HIGH' : 'LOW';";
    html += "});";
    html += "});";
    html += "}";
    html += "function updateTemperatures() {";
    html += "fetch('/temp').then(response => response.json()).then(data => {";
    html += "document.getElementById('temp1').innerText = data.temp1;";
    html += "document.getElementById('temp2').innerText = data.temp2;";
    html += "});";
    html += "}";
    html += "setInterval(updateButtonStates, 1000);";
    html += "setInterval(updateTemperatures, 1000);";
    html += "</script>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><head><link rel='icon' href='data:;base64,iVBORw0KGgo='></head><body>";
    html += "<h1>Configuration</h1>";
    // html += "<form action=\"/save\" method=\"POST\">";
    html += "<form onsubmit='saveConfig(event)'>";
    html += "<label for='ssid'>SSID:</label><input type='text' id='ssid' name='ssid' value='" + config.ssid + "' onfocus='scanWifi()'><br>";
    html += "<label for=\"password\">Password:</label><input type=\"password\" id=\"password\" name=\"password\" value=\"" + config.password + "\"><br>";
    html += "<label for=\"mqtt_server\">MQTT Server:</label><input type=\"text\" id=\"mqtt_server\" name=\"mqtt_server\" value=\"" + config.mqtt_server + "\"><br>";
    html += "<label for=\"mqtt_user\">MQTT User:</label><input type=\"text\" id=\"mqtt_user\" name=\"mqtt_user\" value=\"" + config.mqtt_user + "\"><br>";
    html += "<label for=\"mqtt_password\">MQTT Password:</label><input type=\"password\" id=\"mqtt_password\" name=\"mqtt_password\" value=\"" + config.mqtt_password + "\"><br>";
    html += "<label for=\"sensor1_name\">Sensor 1 Name:</label><input type=\"text\" id=\"sensor1_name\" name=\"sensor1_name\" value=\"" + config.sensor1_name + "\"><br>";
    html += "<label for=\"sensor2_name\">Sensor 2 Name:</label><input type=\"text\" id=\"sensor2_name\" name=\"sensor2_name\" value=\"" + config.sensor2_name + "\"><br>";
    html += "<label for=\"oneWireBus_pin\">OneWire Bus Pin:</label><input type=\"text\" id=\"oneWireBus_pin\" name=\"oneWireBus_pin\" value=\"" + String(config.oneWireBus_pin) + "\"><br>";

    for (int i = 0; i < config.buttons.size(); i++) {
      ButtonConfig button = config.buttons[i];
      html += "<h3>Button " + String(i+1) + "</h3>";
      html += "<label for=\"button_name" + String(i) + "\">Name:</label><input type=\"text\" id=\"button_name" + String(i) + "\" name=\"button_name" + String(i) + "\" value=\"" + button.name + "\"><br>";
      html += "<label for=\"button_pin" + String(i) + "\">Pin:</label><input type=\"text\" id=\"button_pin" + String(i) + "\" name=\"button_pin" + String(i) + "\" value=\"" + button.pin + "\"><br>";
      html += "<label for=\"button_duration" + String(i) + "\">Duration (ms):</label><input type=\"text\" id=\"button_duration" + String(i) + "\" name=\"button_duration" + String(i) + "\" value=\"" + button.duration + "\"><br>";
      html += "<label for=\"button_topic" + String(i) + "\">MQTT Topic:</label><input type=\"text\" id=\"button_topic" + String(i) + "\" name=\"button_topic" + String(i) + "\" value=\"" + button.topic + "\"><br>";
      html += "<label for=\"button_mode" + String(i) + "\">Mode:</label><select id=\"button_mode" + String(i) + "\" name=\"button_mode" + String(i) + "\">";
      html += "<option value=\"" + String(OUTPUT) + "\" " + (button.mode == OUTPUT ? "selected" : "") + ">OUTPUT</option>";
      html += "<option value=\"" + String(INPUT) + "\" " + (button.mode == INPUT ? "selected" : "") + ">INPUT</option>";
      html += "<option value=\"" + String(INPUT_PULLUP) + "\" " + (button.mode == INPUT_PULLUP ? "selected" : "") + ">INPUT_PULLUP</option>";
      html += "<option value=\"" + String(INPUT_PULLDOWN) + "\" " + (button.mode == INPUT_PULLDOWN ? "selected" : "") + ">INPUT_PULLDOWN</option>";
      html += "</select><br>";
      html += "<button type='button' onclick=\"deleteButton(" + String(i) + ")\">Delete</button>";
    }
    
    html += "<button type=\"button\" onclick=\"addButton()\">Add Button</button><br>";
    html += "<input type=\"submit\" value=\"Save\">";
    html += "<button onclick=\"location.href='/'\">BACK</button><br>";
    html += "<button onclick=\"location.href='/ota'\">OTA FW Update</button><br>";
    // html += "<form action=\"/\" method=\"GET\"><button type=\"submit\">Back</button></form>";
    html += "</form>";
    html += "<script>";
    html += "function addButton() {";
    html += "const form = document.querySelector('form');";
    html += "const buttonCount = form.querySelectorAll('h3').length;";
    html += "const html = `<h3>Button ${buttonCount+1}</h3>` +";
    html += "`<label for=\"button_name${buttonCount}\">Name:</label><input type=\"text\" id=\"button_name${buttonCount}\" name=\"button_name${buttonCount}\"><br>` +";
    html += "`<label for=\"button_pin${buttonCount}\">Pin:</label><input type=\"text\" id=\"button_pin${buttonCount}\" name=\"button_pin${buttonCount}\"><br>` +";
    html += "`<label for=\"button_duration${buttonCount}\">Duration (ms):</label><input type=\"text\" id=\"button_duration${buttonCount}\" name=\"button_duration${buttonCount}\"><br>` +";
    html += "`<label for=\"button_topic${buttonCount}\">MQTT Topic:</label><input type=\"text\" id=\"button_topic${buttonCount}\" name=\"button_topic${buttonCount}\"><br>` +";
    html += "`<label for=\"button_mode${buttonCount}\">Mode:</label><select id=\"button_mode${buttonCount}\" name=\"button_mode${buttonCount}\">` +";
    html += "`<option value=\"" + String(OUTPUT) + "\">OUTPUT</option>` +";
    html += "`<option value=\"" + String(INPUT) + "\">INPUT</option>` +";
    html += "`<option value=\"" + String(INPUT_PULLUP) + "\">INPUT_PULLUP</option>` +";
    html += "`<option value=\"" + String(INPUT_PULLDOWN) + "\">INPUT_PULLDOWN</option>` +";
    html += "`</select><br>` +";
    html += "`<button onclick=\"deleteButton${buttonCount}\">Delete</button>` +";
    html += "`<button type=\"button\" onclick=\"addButton()\">Add Button</button><br>`;";
    html += "form.insertAdjacentHTML('beforeend', html);";
    html += "}";
    html += "function scanWifi() {";
    html += "  var xhttp = new XMLHttpRequest();";
    html += "  xhttp.onreadystatechange = function() {";
    html += "    if (this.readyState == 4 && this.status == 200) {";
    html += "      var networks = JSON.parse(this.responseText);";
    html += "      var ssidField = document.getElementById('ssid');";
    html += "      var datalist = document.createElement('datalist');";
    html += "      datalist.id = 'ssid_list';";
    html += "      networks.forEach(function(network) {";
    html += "        var option = document.createElement('option');";
    html += "        option.value = network;";
    html += "        datalist.appendChild(option);";
    html += "      });";
    html += "      ssidField.setAttribute('list', 'ssid_list');";
    html += "      ssidField.parentNode.insertBefore(datalist, ssidField.nextSibling);";
    html += "    }";
    html += "  };";
    html += "  xhttp.open('GET', '/scan', true);";
    html += "  xhttp.send();";
    html += "}";
    html += "function deleteButton(index) {";
    html += "  var xhttp = new XMLHttpRequest();";
    html += "  xhttp.open('POST', '/deleteButton', true);";
    html += "  xhttp.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');";
    html += "  xhttp.onreadystatechange = function() {";
    html += "    if (this.readyState == 4 && this.status == 200) {";
    html += "      location.reload();";
    html += "    }";
    html += "  };";
    html += "  xhttp.send('index=' + index);";
    html += "}";
    html += "function saveConfig(event) {";
    html += "  event.preventDefault();";
    html += "  var form = event.target;";
    html += "  var data = new FormData(form);";
    html += "  var xhttp = new XMLHttpRequest();";
    html += "  xhttp.open('POST', '/save', true);";
    html += "  xhttp.onreadystatechange = function() {";
    html += "    if (this.readyState == 4 && this.status == 200) {";
    html += "      setTimeout(\"alert('Configuration saved');\",1);";
    html += "    }";
    html += "  };";
    html += "  xhttp.send(data);";
    html += "}";
    html += "</script>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, handleSaveConfig);
  server.on("/deleteButton", HTTP_POST, handleDeleteButton);
  server.on("/restart", HTTP_POST, handleRestart);
  server.on("/temp", HTTP_GET, handleTemperature);
  server.on("/ota", HTTP_GET, handleOTA);
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200);
  }, handleUpdateUpload);
  server.on("/update_url", HTTP_POST, handleUpdateUrl);
  server.on("/trigger", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("pin") && request->hasParam("duration")) {
      int pin = request->getParam("pin")->value().toInt();
      unsigned long duration = request->getParam("duration")->value().toInt();
      digitalWrite(pin, HIGH);
      delay(duration);
      digitalWrite(pin, LOW);
      request->send(200, "text/plain", "PIN " + String(pin) + " Triggered for " + String(duration) + " ms");
    } else {
      request->send(400, "text/plain", "Invalid parameters");
    }
  });
  server.on("/button_state", HTTP_GET, handleButtonState);
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "[";
    int n = WiFi.scanComplete();
    if (n == -2) {
      WiFi.scanNetworks(true);
    } else if (n) {
      for (int i = 0; i < n; ++i) {
        if (i) json += ",";
        json += "\"" + WiFi.SSID(i) + "\"";
      }
      WiFi.scanDelete();
      if (WiFi.scanComplete() == -2) {
        WiFi.scanNetworks(true);
      }
    }
    json += "]";
    request->send(200, "application/json", json);
  });
  server.begin();
  sensors->begin();

}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(deviceID.c_str(), config.mqtt_user.c_str(), config.mqtt_password.c_str())) {
      Serial.println("connected");
      Serial.println(WiFi.localIP());
      for (ButtonConfig button : config.buttons) {
        client.subscribe(button.topic.c_str());
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      Serial.println(WiFi.localIP());
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  delay(100);
}
