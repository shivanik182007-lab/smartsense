#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "SPIFFS.h"
#include <LittleFS.h>

#include <ArduinoOTA.h>

#define FIRMWARE_VERSION "v2.2-IR-DHT22"

#define IR_SENSOR_PIN 4

WebServer server(80);
Preferences preferences;


// =========================================================
// GLOBAL CONFIGURATION
// =========================================================

String ssid = "";
String password = "";

String serverIP = "";
String serverPort = "";
String apiPath = "";

String sensorType = "IR";
int sensorPin = 4;
String sensorName = "Sensor 1";

// =====================================================
// SECOND SENSOR CONFIGURATION
// =====================================================
#define SENSOR2_DEFAULT_PIN 5
#define SENSOR2_DEFAULT_ENABLED true
#define SENSOR2_DEFAULT_TYPE "DHT22"

String sensor2Name = "DHT22 Temperature";
String sensor2Type = SENSOR2_DEFAULT_TYPE;
int sensor2Pin = SENSOR2_DEFAULT_PIN;
bool sensor2Enabled = SENSOR2_DEFAULT_ENABLED;
float lastDHT22Temperature = 0.0f;
float lastDHT22Humidity = 0.0f;
bool lastDHT22Valid = false;

String ipMode = "";
String staticIP = "";
String gateway = "";
String subnet = "";
String dns = "";

String sensorURL = "";

// =====================================================
// WEB OTA PROGRESS VARIABLES
// =====================================================

size_t webOtaUploaded = 0;
size_t webOtaTotal = 0;

int webOtaLastPercent = -1;

unsigned long webOtaStartTime = 0;

unsigned long otaUploadStartTime = 0;
size_t otaUploadTotal = 0;
int otaUploadLastPercent = -1;
size_t otaUploadBytes = 0;
size_t otaFileSize = 0;
size_t otaUploaded = 0;

int otaLastPercent = -1;

unsigned long otaStartTime = 0;

// Web OTA state
bool webOtaUpdateStarted = false;
bool webOtaUploadSuccess = false;
bool webOtaUploadFailed = false;
bool webOtaRestartPending = false;
unsigned long webOtaRestartAt = 0;
String webOtaErrorMessage = "";

String checkURL = "";
String firmwareURL = "";
String ackURL = "";




// =========================================================
// SAVED CONFIGURATION
// =========================================================

String savedSSID;
String savedPassword;
String savedServer;
String savedPort;
String savedPath;


// =========================================================
// CURRENT FLASK SERVER
// =========================================================

const char *FLASK_SERVER_IP = "10.153.121.191";
const int FLASK_SERVER_PORT = 5000;


// =========================================================
// CONFIGURATION PORTAL HTML
// =========================================================

const char webpage[] PROGMEM = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<title>ESP32 Configuration</title>

<style>

body{
font-family:Arial;
text-align:center;
margin-top:40px;
background:#f2f2f2;
}

input{
width:250px;
padding:10px;
margin:8px;
}

button{
padding:10px 20px;
font-size:18px;
}

</style>

</head>

<body>

<h2>ESP32 WiFi Configuration</h2>

<form action="/save">

<input name="ssid" placeholder="WiFi SSID"><br>

<input name="password" placeholder="Password"><br>

<input name="server" placeholder="Server IP"><br>

<input name="port" placeholder="5000"><br>

<input name="path" placeholder="/sensor"><br>

<button type="submit">
Save
</button>

</form>

</body>

</html>

)rawliteral";


// =========================================================
// FUNCTION DECLARATIONS
// =========================================================

void handleRoot();
void handleCSS();
void handleJS();

void handleSave();

void startConfigPortal();

bool connectToSavedWiFi();

void startOTA();

void sendSensorData();

void checkOTA();

void sendOTAACK();

void handleSaveNetwork();

void handleSaveSensor();
void handleSaveSensor2();
bool readDHT22(float &temperature, float &humidity);

// =========================================================
// SAVE SECOND SENSOR SETTINGS
// =========================================================
void handleSaveSensor2()
{
    if (!server.hasArg("plain"))
    {
        server.send(400, "text/plain", "No Data");
        return;
    }

    JsonDocument doc;
    DeserializationError error =
        deserializeJson(doc, server.arg("plain"));

    if (error)
    {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    String newName = doc["sensor_name"] | "Sensor 2";
    String newType = doc["sensor_type"] | SENSOR2_DEFAULT_TYPE;
    int newPin = doc["gpio"] | SENSOR2_DEFAULT_PIN;
    bool newEnabled = doc["enabled"] | SENSOR2_DEFAULT_ENABLED;

    if (newName.length() == 0)
        newName = "Sensor 2";

    if (newType.length() == 0)
        newType = SENSOR2_DEFAULT_TYPE;

    if (newPin < 0 || newPin > 39)
    {
        server.send(400, "text/plain", "Invalid GPIO");
        return;
    }

    if (newPin == sensorPin)
    {
        server.send(
            409,
            "text/plain",
            "Sensor 2 GPIO conflicts with Sensor 1 GPIO"
        );
        return;
    }

    preferences.begin("sensor2", false);

    preferences.putString("name", newName);
    preferences.putString("type", newType);
    preferences.putInt("pin", newPin);
    preferences.putBool("enabled", newEnabled);

    preferences.end();

    sensor2Name = newName;
    sensor2Type = newType;
    sensor2Pin = newPin;
    sensor2Enabled = newEnabled;

    if (sensor2Type.equalsIgnoreCase("DHT22"))
        pinMode(sensor2Pin, INPUT_PULLUP);
    else
        pinMode(sensor2Pin, INPUT);

    Serial.println();
    Serial.println("========== SENSOR 2 CONFIG ==========");
    Serial.print("Name    : ");
    Serial.println(sensor2Name);
    Serial.print("Type    : ");
    Serial.println(sensor2Type);
    Serial.print("GPIO    : ");
    Serial.println(sensor2Pin);
    Serial.print("Enabled : ");
    Serial.println(sensor2Enabled ? "true" : "false");
    Serial.println("======================================");

    server.send(
        200,
        "application/json",
        "{\"status\":\"success\"}"
    );
}


void handleStatus();

void startWebServer();

void handleOTAUpload();

void checkNetworkUpdate();

void checkSensorUpdate();


// =========================================================
// ROOT
// =========================================================

void handleRoot()
{
    Serial.println("ROOT PAGE REQUESTED");

    File file = LittleFS.open("/index.html", "r");

    if (!file)
    {
        Serial.println("index.html NOT FOUND");

        server.send(
            404,
            "text/plain",
            "index.html not found"
        );

        return;
    }

    Serial.print("index.html size = ");
    Serial.println(file.size());

    server.streamFile(
        file,
        "text/html"
    );

    file.close();
}


// =========================================================
// CSS
// =========================================================

void handleCSS()
{
    File file = LittleFS.open(
        "/style.css",
        "r"
    );

    if (!file)
    {
        server.send(
            404,
            "text/plain",
            "style.css not found"
        );

        return;
    }

    server.streamFile(
        file,
        "text/css"
    );

    file.close();
}


// =========================================================
// JAVASCRIPT
// =========================================================

void handleJS()
{
    File file = LittleFS.open(
        "/script.js",
        "r"
    );

    if (!file)
    {
        server.send(
            404,
            "text/plain",
            "script.js not found"
        );

        return;
    }

    server.streamFile(
        file,
        "application/javascript"
    );

    file.close();
}


// =========================================================
// OLD CONFIGURATION SAVE
// =========================================================

void handleSave()
{
    ssid = server.arg("ssid");
    password = server.arg("password");
    serverIP = server.arg("server");
    serverPort = server.arg("port");
    apiPath = server.arg("path");


    Serial.println();
    Serial.println("================================");
    Serial.println("DATA RECEIVED FROM WEBPAGE");
    Serial.println("================================");

    Serial.print("SSID      : ");
    Serial.println(ssid);

    Serial.print("Password  : ");
    Serial.println(password);

    Serial.print("Server IP : ");
    Serial.println(serverIP);

    Serial.print("Port      : ");
    Serial.println(serverPort);

    Serial.print("API Path  : ");
    Serial.println(apiPath);


    // Default values

    if (serverIP == "")
    {
        serverIP = FLASK_SERVER_IP;
    }

    if (serverPort == "")
    {
        serverPort = "5000";
    }

    if (apiPath == "")
    {
        apiPath = "/sensor";
    }


    preferences.begin(
        "config",
        false
    );

    preferences.putString(
        "ssid",
        ssid
    );

    preferences.putString(
        "password",
        password
    );

    preferences.putString(
        "server",
        serverIP
    );

    preferences.putString(
        "port",
        serverPort
    );

    preferences.putString(
        "path",
        apiPath
    );

    preferences.putString(
        "ip_mode",
        "dynamic"
    );

    preferences.putString(
        "static_ip",
        ""
    );

    preferences.putString(
        "gateway",
        ""
    );

    preferences.putString(
        "subnet",
        ""
    );

    preferences.putString(
        "dns",
        "");

    preferences.end();


    server.send(
        200,
        "text/html",
        "<h2>Configuration Saved Successfully!</h2>"
        "<p>ESP32 will restart in 3 seconds...</p>"
    );


    Serial.println();
    Serial.println("Configuration saved.");
    Serial.println("Restarting ESP32...");


    delay(3000);

    ESP.restart();
}


// =========================================================
// SAVE NETWORK SETTINGS
// =========================================================

void handleSaveNetwork()
{
    if (!server.hasArg("plain"))
    {
        server.send(
            400,
            "text/plain",
            "No Data"
        );

        return;
    }


    JsonDocument doc;

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );


    if (error)
    {
        Serial.println(
            "JSON Parsing Failed"
        );

        server.send(
            400,
            "text/plain",
            "Invalid JSON"
        );

        return;
    }


    String mode =
        doc["mode"] | "station";

    String newSSID =
        doc["ssid"] | "";

    String newPassword =
        doc["password"] | "";

    String newIPMode =
        doc["ip_mode"] | "dynamic";

    String newStaticIP =
        doc["static_ip"] | "";

    String newGateway =
        doc["gateway"] | "";

    String newSubnet =
        doc["subnet"] | "";

    String newDNS =
        doc["dns"] | "";

    String newServerIP =
        doc["server_ip"] | "";

    String newPort =
        doc["port"] | "5000";

    String newApiPath =
        doc["api_path"] | "/sensor";


    // =====================================================
    // PRINT SETTINGS
    // =====================================================

    Serial.println();
    Serial.println("================================");
    Serial.println("NETWORK SETTINGS RECEIVED");
    Serial.println("================================");

    Serial.print("Mode       : ");
    Serial.println(mode);

    Serial.print("SSID       : ");
    Serial.println(newSSID);

    Serial.print("Password   : ");
    Serial.println(newPassword);

    Serial.print("IP Mode    : ");
    Serial.println(newIPMode);

    Serial.print("Static IP  : ");
    Serial.println(newStaticIP);

    Serial.print("Gateway    : ");
    Serial.println(newGateway);

    Serial.print("Subnet     : ");
    Serial.println(newSubnet);

    Serial.print("DNS        : ");
    Serial.println(newDNS);

    Serial.print("Server IP  : ");
    Serial.println(newServerIP);

    Serial.print("Port       : ");
    Serial.println(newPort);

    Serial.print("API Path   : ");
    Serial.println(newApiPath);


    // =====================================================
    // SAVE TO ESP32 NVS
    // =====================================================

    preferences.begin(
        "config",
        false
    );

    preferences.putString(
        "ssid",
        newSSID
    );

    preferences.putString(
        "password",
        newPassword
    );

    preferences.putString(
        "server",
        newServerIP
    );

    preferences.putString(
        "port",
        newPort
    );

    preferences.putString(
        "path",
        newApiPath
    );

    preferences.putString(
        "mode",
        mode
    );

    preferences.putString(
        "ip_mode",
        newIPMode
    );

    preferences.putString(
        "static_ip",
        newStaticIP
    );

    preferences.putString(
        "gateway",
        newGateway
    );

    preferences.putString(
        "subnet",
        newSubnet
    );

    preferences.putString(
        "dns",
        newDNS
    );

    preferences.end();


    Serial.println();
    Serial.println(
        "Network configuration saved to ESP32."
    );


    // =====================================================
    // FORWARD TO FLASK
    // =====================================================

    String flaskURL =
        "http://" +
        String(FLASK_SERVER_IP) +
        ":" +
        String(FLASK_SERVER_PORT) +
        "/save_network";


    Serial.println();
    Serial.println("================================");
    Serial.println("FORWARDING SETTINGS TO FLASK");
    Serial.println("================================");

    Serial.print("Flask URL : ");
    Serial.println(flaskURL);


    HTTPClient http;

    http.begin(flaskURL);

    http.addHeader(
        "Content-Type",
        "application/json"
    );


    JsonDocument flaskDoc;

    flaskDoc["mode"] = mode;
    flaskDoc["ssid"] = newSSID;
    flaskDoc["password"] = newPassword;

    flaskDoc["ip_mode"] = newIPMode;
    flaskDoc["static_ip"] = newStaticIP;
    flaskDoc["gateway"] = newGateway;
    flaskDoc["subnet"] = newSubnet;
    flaskDoc["dns"] = newDNS;

    flaskDoc["server_ip"] = newServerIP;
    flaskDoc["port"] = newPort;
    flaskDoc["api_path"] = newApiPath;


    String flaskData;

    serializeJson(
        flaskDoc,
        flaskData
    );


    Serial.println();
    Serial.println("JSON SENT TO FLASK:");
    Serial.println(flaskData);


    int response =
        http.POST(flaskData);


    Serial.println();
    Serial.print(
        "Flask HTTP Response : "
    );

    Serial.println(response);


    if (response > 0)
    {
        Serial.println(
            "Settings successfully sent to Flask."
        );
    }
    else
    {
        Serial.println(
            "ERROR: Could not send settings to Flask."
        );

        Serial.print(
            "HTTP Error : "
        );

        Serial.println(response);
    }


    http.end();


    // =====================================================
    // RESPOND TO DASHBOARD
    // =====================================================

    server.send(
        200,
        "application/json",
        "{\"status\":\"ok\"}"
    );


    Serial.println();
    Serial.println("================================");
    Serial.println("Restarting ESP32...");
    Serial.println("================================");


    delay(1000);

    ESP.restart();
}


// =========================================================
// SAVE SENSOR SETTINGS
// =========================================================

void handleSaveSensor()
{
    if (!server.hasArg("plain"))
    {
        server.send(
            400,
            "text/plain",
            "No Data"
        );

        return;
    }


    JsonDocument doc;

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );


    if (error)
    {
        server.send(
            400,
            "text/plain",
            "Invalid JSON"
        );

        return;
    }


    String sensorName =
        doc["sensor_name"] | "Sensor";

    String newSensorType =
        doc["sensor_type"] | "IR";

    int gpio =
        doc["gpio"] | 4;

    String pinModeValue =
        doc["pin_mode"] | "Digital Input";

    String status =
        doc["status"] | "Enabled";


    preferences.begin(
        "sensor",
        false
    );


    preferences.putString(
        "name",
        sensorName
    );

    preferences.putString(
        "type",
        newSensorType
    );

    preferences.putInt(
        "pin",
        gpio
    );

    preferences.putString(
        "pin_mode",
        pinModeValue
    );

    preferences.putString(
        "status",
        status
    );


    preferences.end();


    sensorType =
        newSensorType;

    sensorPin =
        gpio;


    pinMode(
        sensorPin,
        INPUT
    );


    Serial.println();
    Serial.println("========== SENSOR CONFIG ==========");

    Serial.print("Name      : ");
    Serial.println(sensorName);

    Serial.print("Type      : ");
    Serial.println(sensorType);

    Serial.print("GPIO      : ");
    Serial.println(sensorPin);

    Serial.print("Pin Mode  : ");
    Serial.println(pinModeValue);

    Serial.print("Status    : ");
    Serial.println(status);

    Serial.println(
        "==================================="
    );


    server.send(
        200,
        "application/json",
        "{\"status\":\"success\"}"
    );
}


// =========================================================
// STATUS
// =========================================================

void handleStatus()
{
    JsonDocument doc;


    preferences.begin(
        "sensor",
        true
    );


    String sensorName =
        preferences.getString(
            "name",
            "Sensor"
        );

    String storedSensorType =
        preferences.getString(
            "type",
            "IR"
        );

    int gpio =
        preferences.getInt(
            "pin",
            4
        );

    String pinModeValue =
        preferences.getString(
            "pin_mode",
            "Digital Input"
        );

    String status =
        preferences.getString(
            "status",
            "Enabled"
        );


    preferences.end();

    preferences.begin("sensor2", false);

    String sensor2StatusName =
        preferences.getString("name", "Sensor 2");

    String sensor2StatusType =
        preferences.getString("type", SENSOR2_DEFAULT_TYPE);

    int sensor2StatusGPIO =
        preferences.getInt("pin", SENSOR2_DEFAULT_PIN);

    bool sensor2StatusEnabled =
        preferences.getBool("enabled", SENSOR2_DEFAULT_ENABLED);

    preferences.end();


    // =====================================================
    // SENSOR STATUS
    // =====================================================

    if (digitalRead(sensorPin) == LOW)
    {
        doc["sensor_status"] =
            "Object Detected";
    }
    else
    {
        doc["sensor_status"] =
            "No Object";
    }


    // =====================================================
    // WIFI STATUS
    // =====================================================

    doc["wifi_status"] =
        (WiFi.status() == WL_CONNECTED)
        ? "Connected"
        : "Disconnected";


    doc["ip_address"] =
        (WiFi.status() == WL_CONNECTED)
        ? WiFi.localIP().toString()
        : "0.0.0.0";


    // =====================================================
    // WIFI MODE
    // =====================================================

    if (WiFi.getMode() == WIFI_AP)
    {
        doc["wifi_mode"] = "ap";
    }
    else if (WiFi.getMode() == WIFI_STA)
    {
        doc["wifi_mode"] = "station";
    }
    else if (WiFi.getMode() == WIFI_AP_STA)
    {
        doc["wifi_mode"] = "ap_sta";
    }
    else
    {
        doc["wifi_mode"] = "unknown";
    }


    // =====================================================
    // AP IP
    // =====================================================

    doc["ap_ip"] =
        WiFi.softAPIP().toString();


    // =====================================================
    // FIRMWARE
    // =====================================================

    doc["current_version"] =
        FIRMWARE_VERSION;

    doc["latest_version"] =
        FIRMWARE_VERSION;


    // =====================================================
    // SENSOR CONFIGURATION
    // =====================================================

    doc["sensor_name"] =
    sensorName;

    doc["sensor_type"] =
        storedSensorType;

    doc["gpio"] =
        gpio;

    doc["pin_mode"] =
        pinModeValue;

    doc["sensor_enabled"] =
        status;

    doc["sensor2_name"] =
        sensor2StatusName;

    doc["sensor2_type"] =
        sensor2StatusType;

    doc["sensor2_gpio"] =
        sensor2StatusGPIO;

    doc["sensor2_enabled"] =
        sensor2StatusEnabled;

    if (!sensor2StatusEnabled)
    {
        doc["sensor2_status"] = "Disabled";
    }
    else if (sensor2StatusType.equalsIgnoreCase("DHT22"))
    {
        if (lastDHT22Valid)
        {
            doc["sensor2_status"] = "OK";
            doc["sensor2_temperature"] = lastDHT22Temperature;
            doc["sensor2_humidity"] = lastDHT22Humidity;
        }
        else
        {
            doc["sensor2_status"] = "Waiting for DHT22";
        }
    }
    else
    {
        doc["sensor2_status"] =
            (digitalRead(sensor2StatusGPIO) == LOW)
            ? "Object Detected"
            : "No Object";
    }


    // =====================================================
    // DEVICE INFORMATION
    // =====================================================

    doc["mac_address"] =
        WiFi.macAddress();

    doc["hostname"] =
        WiFi.getHostname();


    // =====================================================
    // SEND JSON
    // =====================================================

    String response;

    serializeJson(
        doc,
        response
    );


    server.send(
        200,
        "application/json",
        response
    );
}
void sendOTAProgress(
    int percent,
    size_t uploaded,
    size_t total,
    float speed
)
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    String url =
        "http://" + serverIP + ":" + serverPort +
        "/ota_progress?value=" + String(percent) +
        "&uploaded=" + String((unsigned long)uploaded) +
        "&total=" + String((unsigned long)total) +
        "&speed=" + String(speed, 1);

    HTTPClient progress;
    progress.begin(url);
    progress.setTimeout(500);
    int response = progress.GET();
    progress.end();

    if (response <= 0)
        Serial.print(" [Flask Progress Failed]");
}


// =========================================================
// CORS HELPER FOR BROWSER OTA
// =========================================================

void addCORSHeaders()
{
    server.sendHeader("Access-Control-Allow-Origin", "*", true);
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS", true);
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type", true);
    server.sendHeader("Access-Control-Max-Age", "600", true);
}


// =========================================================
// ESP32 WEB OTA UPLOAD
// =========================================================

void handleOTAUpload()
{
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        Serial.println();
        Serial.println("========================================");
        Serial.println("      WEB OTA UPLOAD STARTED");
        Serial.println("========================================");
        Serial.print("File : ");
        Serial.println(upload.filename);

        otaUploaded = 0;
        otaLastPercent = -1;
        otaStartTime = millis();

        webOtaUpdateStarted = false;
        webOtaUploadSuccess = false;
        webOtaUploadFailed = false;
        webOtaRestartPending = false;
        webOtaErrorMessage = "";

        Serial.println();
        Serial.println("Upload Progress:");

        if (otaFileSize == 0)
        {
            webOtaUploadFailed = true;
            webOtaErrorMessage = "OTA file size was not received";
            Serial.println("OTA BEGIN FAILED");
            Serial.println("Error : OTA file size is 0");
            Serial.println("Make sure /ota_size is called before /upload");
            return;
        }

        Serial.print("Expected File Size : ");
        Serial.print(otaFileSize / 1024.0, 1);
        Serial.println(" KB");
        Serial.println();

        if (!Update.begin(otaFileSize))
        {
            webOtaUploadFailed = true;
            webOtaErrorMessage = Update.errorString();
            Serial.println("OTA BEGIN FAILED");
            Serial.print("Update Error : ");
            Serial.println(Update.errorString());
            return;
        }

        webOtaUpdateStarted = true;
        Serial.println("OTA Update Started Successfully");
        Serial.print("[--------------------]   0%   Uploaded : 0.0 KB / ");
        Serial.print(otaFileSize / 1024.0, 1);
        Serial.println(" KB   Speed : 0.0 KB/s");

        sendOTAProgress(0, 0, otaFileSize, 0.0);
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (!webOtaUpdateStarted || webOtaUploadFailed)
            return;

        size_t written = Update.write(upload.buf, upload.currentSize);

        if (written != upload.currentSize)
        {
            webOtaUploadFailed = true;
            webOtaErrorMessage = Update.errorString();

            Serial.println();
            Serial.println("OTA WRITE ERROR");
            Serial.print("Expected : ");
            Serial.println(upload.currentSize);
            Serial.print("Written  : ");
            Serial.println(written);
            Serial.print("Error    : ");
            Serial.println(Update.errorString());
            return;
        }

        otaUploaded += written;

        if (otaUploaded > otaFileSize)
            otaUploaded = otaFileSize;

        int percent = (int)(((uint64_t)otaUploaded * 100ULL) / otaFileSize);

        if (otaUploaded > 0 && percent < 1)
            percent = 1;

        if (percent > 100)
            percent = 100;

        float speed = 0.0;
        unsigned long elapsed = millis() - otaStartTime;

        if (elapsed > 0)
            speed = (otaUploaded / 1024.0) / (elapsed / 1000.0);

        if (percent != otaLastPercent)
        {
            otaLastPercent = percent;

            int bars = percent / 5;

            Serial.print("\r[");
            for (int i = 0; i < 20; i++)
                Serial.print(i < bars ? '#' : '-');

            Serial.print("] ");
            if (percent < 10)
                Serial.print("  ");
            else if (percent < 100)
                Serial.print(" ");

            Serial.print(percent);
            Serial.print("%   Uploaded : ");
            Serial.print(otaUploaded / 1024.0, 1);
            Serial.print(" KB / ");
            Serial.print(otaFileSize / 1024.0, 1);
            Serial.print(" KB   Speed : ");
            Serial.print(speed, 1);
            Serial.print(" KB/s");
            Serial.flush();

            sendOTAProgress(percent, otaUploaded, otaFileSize, speed);
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        Serial.println();

        if (!webOtaUpdateStarted || webOtaUploadFailed)
        {
            if (!webOtaUploadFailed)
            {
                webOtaUploadFailed = true;
                webOtaErrorMessage = "OTA upload was not started";
            }

            if (webOtaUpdateStarted)
                Update.abort();

            Serial.println("========================================");
            Serial.println("       WEB OTA UPLOAD FAILED");
            Serial.println("========================================");
            return;
        }

        if (otaUploaded != otaFileSize)
        {
            webOtaUploadFailed = true;
            webOtaErrorMessage = "Uploaded size does not match file size";

            Serial.println("========================================");
            Serial.println("       WEB OTA SIZE MISMATCH");
            Serial.println("========================================");
            Serial.print("Expected : ");
            Serial.print(otaFileSize);
            Serial.println(" bytes");
            Serial.print("Uploaded : ");
            Serial.print(otaUploaded);
            Serial.println(" bytes");

            Update.abort();
            return;
        }

        float finalSpeed = 0.0;
        unsigned long elapsed = millis() - otaStartTime;

        if (elapsed > 0)
            finalSpeed = (otaUploaded / 1024.0) / (elapsed / 1000.0);

        if (Update.end(true))
        {
            Serial.print("\r[####################] 100%   Uploaded : ");
            Serial.print(otaUploaded / 1024.0, 1);
            Serial.print(" KB / ");
            Serial.print(otaFileSize / 1024.0, 1);
            Serial.print(" KB   Speed : ");
            Serial.print(finalSpeed, 1);
            Serial.println(" KB/s");

            Serial.println();
            Serial.println("========================================");
            Serial.println("       WEB OTA UPLOAD COMPLETE");
            Serial.println("========================================");
            Serial.print("Total Uploaded : ");
            Serial.print(otaUploaded / 1024.0, 1);
            Serial.println(" KB");
            Serial.print("Average Speed : ");
            Serial.print(finalSpeed, 1);
            Serial.println(" KB/s");
            Serial.println();
            Serial.println("Firmware Installed Successfully");

            sendOTAProgress(100, otaUploaded, otaFileSize, finalSpeed);

            webOtaUploadSuccess = true;
            webOtaUploadFailed = false;
            webOtaRestartPending = true;
            webOtaRestartAt = millis() + 1000;
        }
        else
        {
            webOtaUploadFailed = true;
            webOtaErrorMessage = Update.errorString();

            Serial.println();
            Serial.println("========================================");
            Serial.println("       WEB OTA UPLOAD FAILED");
            Serial.println("========================================");
            Serial.print("Total Uploaded : ");
            Serial.print(otaUploaded / 1024.0, 1);
            Serial.println(" KB");
            Serial.print("OTA UPDATE ERROR : ");
            Serial.println(Update.errorString());
            Serial.println("Firmware NOT Installed");
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        webOtaUploadFailed = true;
        webOtaUploadSuccess = false;
        webOtaErrorMessage = "Upload aborted";

        Serial.println();
        Serial.println("========================================");
        Serial.println("       WEB OTA UPLOAD ABORTED");
        Serial.println("========================================");
        Serial.print("Uploaded : ");
        Serial.print(otaUploaded / 1024.0, 1);
        Serial.println(" KB");

        if (webOtaUpdateStarted)
            Update.abort();
    }
}


// =========================================================
// CONFIGURATION PORTAL
// =========================================================

void startConfigPortal()
{
    Serial.println();
    Serial.println("================================");
    Serial.println("Starting Configuration Portal");
    Serial.println("================================");

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32_Config", "12345678");

    Serial.print("AP IP Address : ");
    Serial.println(WiFi.softAPIP());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/style.css", HTTP_GET, handleCSS);
    server.on("/script.js", HTTP_GET, handleJS);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/save", HTTP_GET, handleSave);
    server.on("/save_network", HTTP_POST, handleSaveNetwork);
    server.on("/save_sensor", HTTP_POST, handleSaveSensor);
    server.on("/save_sensor2", HTTP_POST, handleSaveSensor2);

    server.begin();

    Serial.println();
    Serial.println("Configuration Portal Started");
    Serial.println("SSID     : ESP32_Config");
    Serial.println("Password : 12345678");
    Serial.println("URL      : http://192.168.4.1");
}


// START WEB SERVER
// =========================================================

void startWebServer()
{
    Serial.println();
    Serial.println(">>> Before startWebServer");

    Serial.println("Entered startWebServer()");

    // =====================================================
    // ROOT PAGE
    // =====================================================

    server.on(
        "/",
        HTTP_GET,
        []()
        {
            Serial.println("ROOT PAGE REQUESTED");

            File file =
                LittleFS.open(
                    "/index.html",
                    "r"
                );

            if (!file)
            {
                server.send(
                    404,
                    "text/plain",
                    "index.html not found"
                );

                return;
            }

            Serial.print(
                "index.html size = "
            );

            Serial.println(
                file.size()
            );

            server.streamFile(
                file,
                "text/html"
            );

            file.close();
        }
    );

    Serial.println("Registered /");


    // =====================================================
    // STYLE.CSS
    // =====================================================

    server.on(
        "/style.css",
        HTTP_GET,
        []()
        {
            File file =
                LittleFS.open(
                    "/style.css",
                    "r"
                );

            if (!file)
            {
                server.send(
                    404,
                    "text/plain",
                    "style.css not found"
                );

                return;
            }

            server.streamFile(
                file,
                "text/css"
            );

            file.close();
        }
    );

    Serial.println(
        "Registered style"
    );


    // =====================================================
    // SCRIPT.JS
    // =====================================================

    server.on(
        "/script.js",
        HTTP_GET,
        []()
        {
            File file =
                LittleFS.open(
                    "/script.js",
                    "r"
                );

            if (!file)
            {
                server.send(
                    404,
                    "text/plain",
                    "script.js not found"
                );

                return;
            }

            server.streamFile(
                file,
                "application/javascript"
            );

            file.close();
        }
    );

    Serial.println(
        "Registered js"
    );


    // =====================================================
    // STATUS
    // =====================================================

    server.on(
        "/status",
        HTTP_GET,
        []()
        {
            server.send(
                200,
                "text/plain",
                "ESP32 ONLINE"
            );
        }
    );

    Serial.println(
        "Registered status"
    );


    // =====================================================
    // SAVE NETWORK
    // =====================================================

    server.on(
        "/save_network",
        HTTP_POST,
        []()
        {
            // ------------------------------------------------
            // KEEP YOUR EXISTING NETWORK SAVE CODE HERE
            // ------------------------------------------------

            server.send(
                200,
                "text/plain",
                "Network settings saved"
            );
        }
    );

    Serial.println(
        "Registered save_network"
    );


    // =====================================================
    // SAVE SENSOR
    // =====================================================

    server.on(
        "/save_sensor",
        HTTP_POST,
        handleSaveSensor
    );

    server.on(
        "/save_sensor2",
        HTTP_POST,
        handleSaveSensor2
    );

    Serial.println(
        "Registered save_sensor"
    );


    // =====================================================
    // RECEIVE OTA FILE SIZE
    // JavaScript sends the exact browser file size BEFORE /upload.
    // Accept both /ota_size?size=123 and a plain POST body of 123.

    server.on(
        "/ota_size",
        HTTP_ANY,
        []()
        {
            // Browser CORS preflight
            if (server.method() == HTTP_OPTIONS)
            {
                addCORSHeaders();
                server.send(204, "text/plain", "");
                return;
            }

            addCORSHeaders();

            Serial.println();
            Serial.println("========== OTA SIZE REQUEST ==========");

            String sizeString = "";

            if (server.hasArg("size"))
                sizeString = server.arg("size");
            else if (server.hasArg("plain"))
                sizeString = server.arg("plain");

            sizeString.trim();

            if (sizeString.length() == 0)
            {
                Serial.println("ERROR: OTA size missing");
                server.send(400, "text/plain", "Missing size");
                return;
            }

            unsigned long parsedSize = strtoul(sizeString.c_str(), NULL, 10);
            otaFileSize = (size_t)parsedSize;
            otaUploaded = 0;
            otaLastPercent = -1;
            otaStartTime = millis();

            if (otaFileSize == 0)
            {
                Serial.println("ERROR: File size is 0");
                server.send(400, "text/plain", "Invalid file size");
                return;
            }

            webOtaUpdateStarted = false;
            webOtaUploadSuccess = false;
            webOtaUploadFailed = false;
            webOtaRestartPending = false;
            webOtaErrorMessage = "";

            Serial.print("OTA File Size : ");
            Serial.print(otaFileSize);
            Serial.println(" bytes");
            Serial.print("OTA File Size : ");
            Serial.print(otaFileSize / 1024.0, 1);
            Serial.println(" KB");
            Serial.println("OTA SIZE ACCEPTED");
            Serial.println("======================================");

            server.send(200, "text/plain", "OK");
        }
    );

    Serial.println(
        "Registered ota_size"
    );


    // =====================================================
    // OTA UPLOAD
    // =====================================================

    // Browser CORS preflight for /upload
    server.on(
        "/upload",
        HTTP_OPTIONS,
        []()
        {
            addCORSHeaders();
            server.send(204, "text/plain", "");
        }
    );

    server.on(
        "/upload",
        HTTP_POST,
        []()
        {
            addCORSHeaders();
            Serial.println("HTTP OTA REQUEST FINISHED");

            if (webOtaUploadSuccess)
            {
                server.send(200, "text/plain", "OTA upload finished successfully");
            }
            else if (webOtaUploadFailed)
            {
                String message = "OTA Update Failed: " + webOtaErrorMessage;
                server.send(500, "text/plain", message);
            }
            else
            {
                server.send(500, "text/plain", "OTA upload did not complete");
            }
        },
        handleOTAUpload
    );

    Serial.println(
        "Registered upload"
    );


    // =====================================================
    // START SERVER
    // =====================================================

    server.begin();

    Serial.println(
        "server.begin() DONE"
    );

    Serial.println(
        "WEB SERVER STARTED"
    );

    Serial.println(
        ">>> After startWebServer"
    );
}


// =========================================================
// CONNECT TO SAVED WIFI
// =========================================================

bool connectToSavedWiFi()
{
    preferences.begin(
        "config",
        true
    );


    savedSSID =
        preferences.getString(
            "ssid",
            ""
        );

    savedPassword =
        preferences.getString(
            "password",
            ""
        );

    savedServer =
        preferences.getString(
            "server",
            ""
        );

    savedPort =
        preferences.getString(
            "port",
            "5000"
        );

    savedPath =
        preferences.getString(
            "path",
            "/sensor"
        );


    String savedIPMode =
        preferences.getString(
            "ip_mode",
            "dhcp"
        );

    String savedStaticIP =
        preferences.getString(
            "static_ip",
            ""
        );

    String savedGateway =
        preferences.getString(
            "gateway",
            ""
        );

    String savedSubnet =
        preferences.getString(
            "subnet",
            ""
        );

    String savedDNS =
        preferences.getString(
            "dns",
            ""
        );

    String savedMode =
        preferences.getString(
            "mode",
            "station"
        );


    Serial.println();
    Serial.println(
        "===== RAW PREFERENCES ====="
    );


    Serial.print(
        "Saved Mode : "
    );

    Serial.println(
        savedMode
    );


    Serial.print(
        "Saved IP Mode : "
    );

    Serial.println(
        savedIPMode
    );


    Serial.print(
        "Saved Static IP : "
    );

    Serial.println(
        savedStaticIP
    );


    Serial.print(
        "Saved Gateway : "
    );

    Serial.println(
        savedGateway
    );


    Serial.print(
        "Saved Subnet : "
    );

    Serial.println(
        savedSubnet
    );


    Serial.print(
        "Saved DNS : "
    );

    Serial.println(
        savedDNS
    );


    Serial.print(
        "SSID : "
    );

    Serial.println(
        savedSSID
    );


    Serial.print(
        "Server IP : "
    );

    Serial.println(
        savedServer
    );


    Serial.print(
        "Port : "
    );

    Serial.println(
        savedPort
    );


    Serial.print(
        "API Path : "
    );

    Serial.println(
        savedPath
    );


    preferences.end();


    // =====================================================
    // SENSOR CONFIG
    // =====================================================

    preferences.begin(
        "sensor",
        false
    );


    sensorName =
        preferences.getString(
            "name",
            "Sensor 1"
        );

    sensorType =
        preferences.getString(
            "type",
            "IR"
        );


    sensorPin =
        preferences.getInt(
            "pin",
            4
        );


    preferences.end();

    preferences.begin("sensor2", false);

    sensor2Name =
        preferences.getString("name", "Sensor 2");

    sensor2Type =
        preferences.getString("type", SENSOR2_DEFAULT_TYPE);

    sensor2Pin =
        preferences.getInt("pin", SENSOR2_DEFAULT_PIN);

    sensor2Enabled =
        preferences.getBool("enabled", SENSOR2_DEFAULT_ENABLED);

    preferences.end();


    // =====================================================
    // ACCESS POINT MODE
    // =====================================================
    // If the saved mode is "ap", the ESP32 hosts its own WiFi
    // hotspot instead of connecting to a router. It will NOT
    // be able to reach the Flask server in this mode unless a
    // device on this hotspot is running Flask itself.

    if (savedMode == "ap")
    {
        Serial.println();
        Serial.println(
            "================================"
        );

        Serial.println(
            "STARTING IN ACCESS POINT MODE"
        );

        Serial.println(
            "================================"
        );


        String apSSID =
            (savedSSID.length() > 0)
                ? savedSSID
                : "ESP32_Hotspot";

        String apPassword =
            (savedPassword.length() >= 8)
                ? savedPassword
                : "12345678";


        WiFi.disconnect(
            true,
            true
        );

        delay(500);

        WiFi.mode(
            WIFI_AP
        );

        bool apStarted =
            WiFi.softAP(
                apSSID.c_str(),
                apPassword.c_str()
            );

        if (!apStarted)
        {
            Serial.println(
                "Failed To Start Access Point"
            );

            return false;
        }


        Serial.print(
            "Access Point SSID : "
        );

        Serial.println(
            apSSID
        );

        Serial.print(
            "Access Point IP   : "
        );

        Serial.println(
            WiFi.softAPIP()
        );


        serverIP =
            (savedServer.length() > 0)
                ? savedServer
                : FLASK_SERVER_IP;

        serverPort =
            (savedPort.length() > 0)
                ? savedPort
                : "5000";

        apiPath =
            (savedPath.length() > 0)
                ? savedPath
                : "/sensor";


        sensorURL =
            "http://" + serverIP + ":" + serverPort + apiPath;

        checkURL =
            "http://" + serverIP + ":" + serverPort + "/check_update";

        firmwareURL =
            "http://" + serverIP + ":" + serverPort + "/firmware.bin";

        ackURL =
            "http://" + serverIP + ":" + serverPort + "/ack";


        Serial.println();
        Serial.println(
            "NOTE: Flask server is unreachable while in Access Point mode"
        );

        Serial.println(
            "unless a device connected to this hotspot is running it."
        );


        return true;
    }


    if (savedSSID == "")
    {
        Serial.println(
            "No Saved Configuration Found"
        );

        return false;
    }


    // =====================================================
    // WIFI MODE
    // =====================================================

    WiFi.disconnect(
        true,
        true
    );

    delay(1000);


    WiFi.mode(
        WIFI_STA
    );

    delay(500);


    WiFi.setSleep(
        false
    );


    // =====================================================
    // STATIC IP
    // =====================================================

    if (savedIPMode == "static")
    {
        IPAddress localIP;
        IPAddress gatewayIP;
        IPAddress subnetIP;
        IPAddress dnsIP;


        localIP.fromString(
            savedStaticIP
        );

        gatewayIP.fromString(
            savedGateway
        );

        subnetIP.fromString(
            savedSubnet
        );

        dnsIP.fromString(
            savedDNS
        );


        Serial.println(
            "Using STATIC IP"
        );


        WiFi.config(
            localIP,
            gatewayIP,
            subnetIP,
            dnsIP
        );
    }
    else
    {
        Serial.println(
            "Using DHCP"
        );
    }


    // =====================================================
    // CONNECT
    // =====================================================

    Serial.print(
        "Connecting to SSID : "
    );

    Serial.println(
        savedSSID
    );


    WiFi.begin(
        savedSSID.c_str(),
        savedPassword.c_str()
    );


    unsigned long start =
        millis();


    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 30000
    )
    {
        Serial.print(
            "."
        );

        delay(500);
    }


    Serial.println();


    Serial.print(
        "Final Status : "
    );

    Serial.println(
        WiFi.status()
    );


    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(
            "WiFi Connection Failed"
        );

        return false;
    }


    // =====================================================
    // CONNECTED
    // =====================================================

    Serial.println();
    Serial.println(
        "WiFi Connected Successfully!"
    );


    Serial.print(
        "ESP32 IP Address : "
    );

    Serial.println(
        WiFi.localIP()
    );


    serverIP =
        savedServer;


    serverPort =
        savedPort;


    apiPath =
        savedPath;


    // =====================================================
    // CURRENT SERVER IP
    // =====================================================

    if (serverIP == "")
    {
        serverIP =
            FLASK_SERVER_IP;
    }


    if (serverPort == "")
    {
        serverPort =
            "5000";
    }


    if (apiPath == "")
    {
        apiPath =
            "/sensor";
    }


    // =====================================================
    // BUILD URLS
    // =====================================================

    sensorURL =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        apiPath;


    checkURL =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/check_update";


    firmwareURL =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/firmware.bin";


    ackURL =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/ack";


    Serial.println();
    Serial.println(
        "===== SERVER URLs ====="
    );


    Serial.println(
        sensorURL
    );

    Serial.println(
        checkURL
    );

    Serial.println(
        firmwareURL
    );

    Serial.println(
        ackURL
    );


    return true;
}

// =========================================================
// ARDUINO OTA
// =========================================================

void startOTA()
{
    Serial.println();
    Serial.println(
        "Starting OTA..."
    );


    ArduinoOTA.setHostname(
        "ESP32-OTA"
    );


    ArduinoOTA.onStart(
        []()
        {
            Serial.println();
            Serial.println(
                "================================"
            );

            Serial.println(
                "OTA UPDATE STARTED"
            );

            Serial.println(
                "================================"
            );
        }
    );


    ArduinoOTA.onEnd(
        []()
        {
            Serial.println();
            Serial.println(
                "================================"
            );

            Serial.println(
                "OTA UPDATE FINISHED"
            );

            Serial.println(
                "================================"
            );
        }
    );


    ArduinoOTA.onProgress(
        [](unsigned int progress,
           unsigned int total)
        {
            Serial.printf(
                "Progress: %u%%\r",
                (progress * 100) / total
            );
        }
    );


    ArduinoOTA.onError(
        [](ota_error_t error)
        {
            Serial.println();

            Serial.print(
                "OTA Error ["
            );

            Serial.print(
                error
            );

            Serial.println(
                "]"
            );


            if (error == OTA_AUTH_ERROR)
                Serial.println(
                    "Authentication Failed"
                );

            else if (error == OTA_BEGIN_ERROR)
                Serial.println(
                    "Begin Failed"
                );

            else if (error == OTA_CONNECT_ERROR)
                Serial.println(
                    "Connection Failed"
                );

            else if (error == OTA_RECEIVE_ERROR)
                Serial.println(
                    "Receive Failed"
                );

            else if (error == OTA_END_ERROR)
                Serial.println(
                    "End Failed"
                );
        }
    );


    ArduinoOTA.begin();


    Serial.println(
        "ArduinoOTA.begin() executed"
    );


    Serial.println(
        "OTA Port: 3232"
    );


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "OTA READY"
    );

    Serial.println(
        "Hostname : ESP32-OTA"
    );

    Serial.println(
        "================================"
    );
}


// =========================================================
// SETUP
// =========================================================

void setup()
{
    // =====================================================
    // SERIAL
    // =====================================================

    Serial.begin(
        115200
    );

    delay(1000);


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "        ESP32 BOOT"
    );

    Serial.println(
        "================================"
    );


    // =====================================================
    // LITTLEFS
    // =====================================================

    if (!LittleFS.begin(true))
    {
        Serial.println(
            "LittleFS Mount Failed"
        );

        return;
    }


    Serial.println(
        "LittleFS Mounted"
    );


    // =====================================================
    // LIST FILES
    // =====================================================

    File root =
        LittleFS.open("/");


    if (!root)
    {
        Serial.println(
            "Failed to open LittleFS root"
        );
    }
    else
    {
        File file =
            root.openNextFile();


        Serial.println();
        Serial.println(
            "===== LittleFS Files ====="
        );


        while (file)
        {
            Serial.print(
                file.name()
            );

            Serial.print(
                "   "
            );

            Serial.println(
                file.size()
            );


            file =
                root.openNextFile();
        }


        Serial.println(
            "=========================="
        );
    }


    // =====================================================
    // BUILD INFORMATION
    // =====================================================

    Serial.println();
    Serial.println(
        "******** FINAL OTA BUILD ********"
    );

    Serial.print(
        "Firmware Version : "
    );

    Serial.println(
        FIRMWARE_VERSION
    );


    Serial.print(
        "Flask Server     : "
    );

    Serial.print(
        FLASK_SERVER_IP
    );

    Serial.print(
        ":"
    );

    Serial.println(
        FLASK_SERVER_PORT
    );


    // =====================================================
    // LOAD SENSOR
    // =====================================================

    preferences.begin(
        "sensor",
        true
    );


    sensorName =
        preferences.getString(
            "name",
            "Sensor 1"
        );

    sensorType =
        preferences.getString(
            "type",
            "IR"
        );


    sensorPin =
        preferences.getInt(
            "pin",
            4
        );


    preferences.end();

    preferences.begin("sensor2", false);

    sensor2Name =
        preferences.getString("name", "Sensor 2");

    sensor2Type =
        preferences.getString("type", SENSOR2_DEFAULT_TYPE);

    sensor2Pin =
        preferences.getInt("pin", SENSOR2_DEFAULT_PIN);

    sensor2Enabled =
        preferences.getBool("enabled", SENSOR2_DEFAULT_ENABLED);

    preferences.end();

    // Sensor 2 is dedicated to the DHT22 on GPIO 5.
    // If an older NVS record disabled Sensor 2, restore the intended default.
    if (sensor2Type.equalsIgnoreCase("DHT22"))
    {
        sensor2Enabled = true;
        preferences.begin("sensor2", false);
        preferences.putBool("enabled", true);
        preferences.end();
    }


    Serial.println();
    Serial.println(
        "===== SENSOR SETTINGS ====="
    );


    Serial.print(
        "Type : "
    );

    Serial.println(
        sensorType
    );


    Serial.print(
        "GPIO : "
    );

    Serial.println(
        sensorPin
    );


    Serial.println(
        "============================"
    );


    // =====================================================
    // SENSOR PIN
    // =====================================================

    pinMode(
        sensorPin,
        INPUT
    );


    Serial.print(
        "Sensor GPIO "
    );

    Serial.print(
        sensorPin
    );

    Serial.println(
        " configured as INPUT"
    );

    if (sensor2Enabled)
    {
        if (sensor2Pin == sensorPin)
        {
            Serial.println(
                "WARNING: Sensor 2 GPIO conflicts with Sensor 1. Sensor 2 disabled."
            );
            sensor2Enabled = false;
        }
        else
        {
            pinMode(
                sensor2Pin,
                INPUT
            );
        }
    }

    Serial.print("Sensor 2 Name : ");
    Serial.println(sensor2Name);
    Serial.print("Sensor 2 GPIO : ");
    Serial.println(sensor2Pin);
    Serial.print("Sensor 2 Enabled : ");
    Serial.println(sensor2Enabled ? "true" : "false");


    // =====================================================
    // WIFI
    // =====================================================

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "Connecting to Saved WiFi"
    );

    Serial.println(
        "================================"
    );


    if (connectToSavedWiFi())
    {
        // ==================================================
        // WIFI CONNECTED
        // ==================================================

        Serial.println();
        Serial.println(
            "================================"
        );

        Serial.println(
            "WiFi Connected"
        );

        Serial.println(
            "================================"
        );


        Serial.print(
            "ESP32 IP Address : "
        );

        Serial.println(
            WiFi.getMode() == WIFI_AP
                ? WiFi.softAPIP()
                : WiFi.localIP()
        );


        Serial.print(
            "WiFi Mode        : "
        );

        Serial.println(
            WiFi.getMode() == WIFI_STA
            ? "STATION"
            : (WiFi.getMode() == WIFI_AP
                ? "ACCESS POINT"
                : "OTHER")
        );


        // ==================================================
        // START OTA
        // ==================================================

        Serial.println();
        Serial.println(
            ">>> Before startOTA"
        );


        startOTA();


        Serial.println(
            ">>> After startOTA"
        );


        // ==================================================
        // START WEB SERVER
        // ==================================================

        Serial.println();
        Serial.println(
            ">>> Before startWebServer"
        );


        startWebServer();


        Serial.println(
            ">>> After startWebServer"
        );


        // ==================================================
        // OTA PENDING
        // ==================================================

        preferences.begin(
            "config",
            false
        );


        bool otaPending =
            preferences.getBool(
                "otaPending",
                false
            );


        Serial.println();
        Serial.println(
            "========== OTA FLAG =========="
        );


        Serial.print(
            "otaPending = "
        );


        if (otaPending)
        {
            Serial.println(
                "TRUE"
            );


            Serial.println();
            Serial.println(
                "Sending OTA ACK..."
            );


            sendOTAACK();


            preferences.putBool(
                "otaPending",
                false
            );


            Serial.println(
                "OTA ACK completed."
            );


            Serial.println(
                "otaPending cleared."
            );
        }
        else
        {
            Serial.println(
                "FALSE"
            );
        }


        Serial.println(
            "=============================="
        );


        preferences.end();
    }
    else
    {
        // ==================================================
        // WIFI FAILED
        // ==================================================

        Serial.println();
        Serial.println(
            "================================"
        );

        Serial.println(
            "WiFi Connection Failed"
        );

        Serial.println(
            "Starting Configuration Portal"
        );

        Serial.println(
            "================================"
        );


        startConfigPortal();
    }


    // =====================================================
    // READY
    // =====================================================

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "        ESP32 READY"
    );

    Serial.println(
        "================================"
    );
}


// =========================================================
// LOOP
// =========================================================

void loop()
{
    // =====================================================
    // ARDUINO OTA
    // =====================================================

    if (WiFi.status() == WL_CONNECTED)
    {
        ArduinoOTA.handle();
    }


    // =====================================================
    // WEB SERVER
    // =====================================================

    server.handleClient();

    if (webOtaRestartPending && millis() >= webOtaRestartAt)
    {
        webOtaRestartPending = false;
        Serial.println();
        Serial.println("Restarting ESP32...");
        Serial.println("========================================");
        delay(200);
        ESP.restart();
    }


    // =====================================================
    // AP CLIENT COUNT
    // =====================================================

    if (WiFi.getMode() == WIFI_AP)
    {
        static unsigned long previousAP = 0;


        if (
            millis() - previousAP >
            3000
        )
        {
            previousAP =
                millis();


            Serial.print(
                "Connected Devices : "
            );


            Serial.println(
                WiFi.softAPgetStationNum()
            );
        }
    }


    // =====================================================
    // SENSOR EVERY 3 SECONDS
    // DHT22 needs a safe interval between measurements.
    // =====================================================

    static unsigned long previousSensor = 0;


    if (
        millis() - previousSensor >=
        3000
    )
    {
        previousSensor =
            millis();


        sendSensorData();
    }


    // =====================================================
    // OTA / NETWORK / SENSOR UPDATE
    // EVERY 10 SECONDS
    // =====================================================

    static unsigned long previousOTA = 0;


    if (
        millis() - previousOTA >=
        10000
    )
    {
        previousOTA =
            millis();


        Serial.println();
        Serial.println(
            "******** 10 SECOND TIMER ********"
        );


        // =================================================
        // OTA
        // =================================================

        Serial.println(
            "STEP 1 - OTA CHECK"
        );


        checkOTA();


        // =================================================
        // NETWORK UPDATE
        // =================================================

        Serial.println(
            "STEP 2 - NETWORK UPDATE"
        );


        checkNetworkUpdate();


        // =================================================
        // SENSOR UPDATE
        // =================================================

        Serial.println(
            "STEP 3 - SENSOR UPDATE"
        );


        checkSensorUpdate();


        Serial.println(
            "******** TIMER DONE ********"
        );
    }
}


// =========================================================
// SEND SENSOR DATA
// =========================================================

void sendSensorData()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    String mac = WiFi.macAddress();

    // =====================================================
    // LOAD CURRENT SENSOR 1 NAME
    // =====================================================
    preferences.begin("sensor", true);
    String sensor1NameToSend =
        preferences.getString("name", "Sensor 1");
    preferences.end();

    // =====================================================
    // SENSOR 1 - IR
    // =====================================================
    int sensorValue1 = digitalRead(sensorPin);

    String sensorStatus1 =
        (sensorValue1 == LOW)
        ? "Object Detected"
        : "No Object";

    HTTPClient http1;
    http1.begin(sensorURL);
    http1.addHeader(
        "Content-Type",
        "application/x-www-form-urlencoded"
    );

    String data1 =
        "status=" + sensorStatus1 +
        "&mac=" + mac +
        "&sensor_id=" + mac +
        "&sensor_name=" + sensor1NameToSend +
        "&sensor_type=" + sensorType +
        "&gpio=" + String(sensorPin) +
        "&value=" + String(sensorValue1);

    int response1 = http1.POST(data1);

    Serial.println();
    Serial.println("--------------------------");
    Serial.println("SENSOR 1 - IR");
    Serial.print("HTTP Response : ");
    Serial.println(response1);
    Serial.print("GPIO : ");
    Serial.println(sensorPin);
    Serial.print("Value : ");
    Serial.println(sensorValue1);
    Serial.print("Status : ");
    Serial.println(sensorStatus1);

    http1.end();

    // =====================================================
    // SENSOR 2
    // =====================================================
    if (!sensor2Enabled)
    {
        Serial.println("SENSOR 2 : DISABLED");
        Serial.println("--------------------------");
        return;
    }

    if (sensor2Pin == sensorPin)
    {
        Serial.println("SENSOR 2 : GPIO CONFLICT - NOT SENT");
        Serial.println("--------------------------");
        return;
    }

    String sensor2ID = mac + "-S2";

    // =====================================================
    // SENSOR 2 - DHT22
    // =====================================================
    if (sensor2Type.equalsIgnoreCase("DHT22"))
    {
        float temperature = NAN;
        float humidity = NAN;

        bool dhtOK = readDHT22(temperature, humidity);

        // A DHT22 can occasionally miss one transaction. Retry once
        // without changing the normal 3-second polling interval.
        if (!dhtOK)
        {
            delay(120);
            dhtOK = readDHT22(temperature, humidity);
        }

        if (dhtOK)
        {
            lastDHT22Temperature = temperature;
            lastDHT22Humidity = humidity;
            lastDHT22Valid = true;
        }

        Serial.println();
        Serial.println("--------------------------");
        Serial.println("SENSOR 2 - DHT22");
        Serial.print("Sensor ID : ");
        Serial.println(sensor2ID);
        Serial.print("GPIO : ");
        Serial.println(sensor2Pin);

        if (!dhtOK)
        {
            Serial.println("DHT22 Read : FAILED");
            Serial.println("Check VCC / GND / DATA wiring");
            Serial.println("--------------------------");
            return;
        }

        Serial.print("Temperature : ");
        Serial.print(temperature, 1);
        Serial.println(" C");

        Serial.print("Humidity : ");
        Serial.print(humidity, 1);
        Serial.println(" %");

        HTTPClient http2;
        http2.begin(sensorURL);
        http2.addHeader(
            "Content-Type",
            "application/x-www-form-urlencoded"
        );

        String data2 =
            "status=OK" +
            String("&mac=") + mac +
            "&sensor_id=" + sensor2ID +
            "&sensor_name=" + sensor2Name +
            "&sensor_type=DHT22" +
            "&gpio=" + String(sensor2Pin) +
            "&value=" + String(temperature, 2) +
            "&temperature=" + String(temperature, 2) +
            "&humidity=" + String(humidity, 2);

        int response2 = http2.POST(data2);

        Serial.print("HTTP Response : ");
        Serial.println(response2);
        Serial.println("DHT22 data sent successfully");
        Serial.println("--------------------------");

        http2.end();
        return;
    }

    // =====================================================
    // SENSOR 2 - IR FALLBACK
    // =====================================================
    int sensorValue2 = digitalRead(sensor2Pin);

    String sensorStatus2 =
        (sensorValue2 == LOW)
        ? "Object Detected"
        : "No Object";

    HTTPClient http2;
    http2.begin(sensorURL);
    http2.addHeader(
        "Content-Type",
        "application/x-www-form-urlencoded"
    );

    String data2 =
        "status=" + sensorStatus2 +
        "&mac=" + mac +
        "&sensor_id=" + sensor2ID +
        "&sensor_name=" + sensor2Name +
        "&sensor_type=" + sensor2Type +
        "&gpio=" + String(sensor2Pin) +
        "&value=" + String(sensorValue2);

    int response2 = http2.POST(data2);

    Serial.println("SENSOR 2 - IR FALLBACK");
    Serial.print("HTTP Response : ");
    Serial.println(response2);
    Serial.print("Sensor Value : ");
    Serial.println(sensorValue2);
    Serial.print("Sensor Status : ");
    Serial.println(sensorStatus2);
    Serial.println("--------------------------");

    http2.end();
}

// =========================================================
// DHT22 READER - NO EXTRA LIBRARY REQUIRED
// =========================================================

bool readDHT22(float &temperature, float &humidity)
{
    const uint8_t pin = (uint8_t)sensor2Pin;
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // DHT22 requires a safe interval between readings.
    static unsigned long lastRead = 0;
    if (lastRead != 0 && millis() - lastRead < 2500)
    {
        return false;
    }

    // Start signal: pull data LOW for at least 1 ms.
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delay(2);
    digitalWrite(pin, HIGH);
    delayMicroseconds(30);
    pinMode(pin, INPUT_PULLUP);

    // Sensor response: LOW ~80 us, then HIGH ~80 us.
    unsigned long timeout = micros();
    while (digitalRead(pin) == HIGH)
    {
        if (micros() - timeout > 120)
            return false;
    }

    timeout = micros();
    while (digitalRead(pin) == LOW)
    {
        if (micros() - timeout > 120)
            return false;
    }

    timeout = micros();
    while (digitalRead(pin) == HIGH)
    {
        if (micros() - timeout > 120)
            return false;
    }

    // Read 40 data bits.
    for (int i = 0; i < 40; i++)
    {
        // Each bit starts with a LOW pulse of about 50 us.
        timeout = micros();
        while (digitalRead(pin) == LOW)
        {
            if (micros() - timeout > 100)
                return false;
        }

        // HIGH pulse length: ~26-28 us = 0, ~70 us = 1.
        unsigned long highStart = micros();
        while (digitalRead(pin) == HIGH)
        {
            if (micros() - highStart > 120)
                return false;
        }

        unsigned long highDuration = micros() - highStart;
        data[i / 8] <<= 1;

        if (highDuration > 45)
            data[i / 8] |= 1;
    }

    lastRead = millis();

    // Checksum.
    uint8_t checksum =
        (uint8_t)(data[0] + data[1] + data[2] + data[3]);

    if (checksum != data[4])
    {
        Serial.println("DHT22 checksum error");
        return false;
    }

    // DHT22 humidity is 16-bit / 10.
    humidity =
        ((data[0] << 8) | data[1]) / 10.0f;

    // DHT22 temperature is signed 16-bit / 10.
    int16_t rawTemperature =
        (int16_t)((data[2] & 0x7F) << 8 | data[3]);

    temperature = rawTemperature / 10.0f;

    if (data[2] & 0x80)
        temperature = -temperature;

    if (humidity < 0.0f || humidity > 100.0f ||
        temperature < -40.0f || temperature > 80.0f)
    {
        Serial.println("DHT22 value out of range");
        return false;
    }

    return true;
}

// =========================================================
// OTA ACK
// =========================================================

void sendOTAACK()
{
    Serial.println();
    Serial.println(
        "################################"
    );

    Serial.println(
        "INSIDE sendOTAACK()"
    );

    Serial.println(
        "################################"
    );


    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(
            "WiFi NOT Connected"
        );
        return;
    }


    HTTPClient http;


    http.begin(
        ackURL
    );


    http.addHeader(
        "Content-Type",
        "application/x-www-form-urlencoded"
    );


    int response =
        http.POST(
            "status=success"
        );


    Serial.print(
        "HTTP Response : "
    );

    Serial.println(
        response
    );


    if (response > 0)
    {
        Serial.println(
            "ACK SENT SUCCESSFULLY"
        );
    }
    else
    {
        Serial.println(
            "ACK FAILED"
        );
    }


    http.end();
}


void checkOTA()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    HTTPClient http;
    http.begin(checkURL);
    int response = http.GET();

    if (response != HTTP_CODE_OK)
    {
        Serial.print("OTA Check Failed : "); Serial.println(response);
        http.end();
        return;
    }

    String result = http.getString();
    result.trim();
    http.end();

    Serial.println();
    Serial.println("========== OTA CHECK ==========");
    Serial.print("Server Response : "); Serial.println(result);

    if (result != "UPDATE")
    {
        Serial.println("No Update");
        return;
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("       NEW FIRMWARE AVAILABLE");
    Serial.println("========================================");

    preferences.begin("config", false);
    preferences.putBool("otaPending", true);
    preferences.end();

    HTTPClient firmware;
    firmware.begin(firmwareURL);
    int firmwareResponse = firmware.GET();

    if (firmwareResponse != HTTP_CODE_OK)
    {
        Serial.print("Firmware Download Failed : "); Serial.println(firmwareResponse);
        firmware.end();
        return;
    }

    int totalSize = firmware.getSize();

    if (totalSize <= 0)
    {
        Serial.println("Invalid Firmware Size");
        firmware.end();
        return;
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("        OTA DOWNLOAD STARTED");
    Serial.println("========================================");
    Serial.print("Firmware Size : ");
    Serial.print(totalSize / 1024.0, 1);
    Serial.println(" KB");

    if (!Update.begin(totalSize))
    {
        Serial.println("Cannot Start OTA");
        Serial.print("Update Error : "); Serial.println(Update.errorString());
        firmware.end();
        return;
    }

    WiFiClient *stream = firmware.getStreamPtr();
    uint8_t buffer[2048];
    size_t downloaded = 0;
    int lastPercent = -1;
    unsigned long startTime = millis();

    sendOTAProgress(0, 0, (size_t)totalSize, 0.0);

    while (downloaded < (size_t)totalSize)
    {
        size_t remaining = (size_t)totalSize - downloaded;
        size_t toRead = sizeof(buffer);
        if (remaining < toRead)
            toRead = remaining;

        int len = stream->readBytes(buffer, toRead);
        if (len <= 0)
        {
            Serial.println();
            Serial.println("Firmware Download Interrupted");
            Update.abort();
            firmware.end();
            return;
        }

        size_t bytesWritten = Update.write(buffer, len);
        if (bytesWritten != (size_t)len)
        {
            Serial.println();
            Serial.println("OTA Write Error");
            Serial.print("Expected : "); Serial.println(len);
            Serial.print("Written  : "); Serial.println(bytesWritten);
            Serial.print("Error    : "); Serial.println(Update.errorString());
            Update.abort();
            firmware.end();
            return;
        }

        downloaded += bytesWritten;

        int percent = (int)(((uint64_t)downloaded * 100ULL) / (uint64_t)totalSize);
        if (downloaded > 0 && percent < 1)
            percent = 1;
        if (percent > 100)
            percent = 100;

        if (percent != lastPercent)
        {
            lastPercent = percent;

            float elapsed = (millis() - startTime) / 1000.0;
            float speed = elapsed > 0 ? (downloaded / 1024.0) / elapsed : 0.0;

            int bars = percent / 5;
            Serial.print("\r[");
            for (int i = 0; i < 20; i++)
                Serial.print(i < bars ? '#' : '-');
            Serial.print("] ");
            if (percent < 10)
                Serial.print("  ");
            else if (percent < 100)
                Serial.print(" ");
            Serial.print(percent);
            Serial.print("%   ");
            Serial.print(downloaded / 1024.0, 1);
            Serial.print(" KB / ");
            Serial.print(totalSize / 1024.0, 1);
            Serial.print(" KB   Speed : ");
            Serial.print(speed, 1);
            Serial.print(" KB/s");
            Serial.flush();

            sendOTAProgress(percent, downloaded, (size_t)totalSize, speed);
        }
    }

    Serial.println();

    float finalSpeed = 0.0;
    unsigned long elapsed = millis() - startTime;
    if (elapsed > 0)
        finalSpeed = (downloaded / 1024.0) / (elapsed / 1000.0);

    if (Update.end(true) && Update.isFinished())
    {
        Serial.println();
        Serial.println("========================================");
        Serial.println("       OTA DOWNLOAD COMPLETE");
        Serial.println("========================================");
        Serial.print("Total Downloaded : ");
        Serial.print(downloaded / 1024.0, 1);
        Serial.println(" KB");
        Serial.print("Average Speed : ");
        Serial.print(finalSpeed, 1);
        Serial.println(" KB/s");
        Serial.println("Firmware Installed Successfully");

        sendOTAProgress(100, downloaded, (size_t)totalSize, finalSpeed);

        Serial.println();
        Serial.println("Restarting ESP32...");
        Serial.println("========================================");
        firmware.end();
        delay(1000);
        ESP.restart();
    }
    else
    {
        Serial.println();
        Serial.print("OTA Update Error : ");
        Serial.println(Update.errorString());
        firmware.end();
    }
}
// =========================================================
// CHECK NETWORK UPDATE
// =========================================================

void checkNetworkUpdate()
{
    if (WiFi.status() != WL_CONNECTED)
        return;


    HTTPClient http;


    String url =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/network_update";


    Serial.println();
    Serial.println(
        "========== NETWORK UPDATE =========="
    );


    Serial.print(
        "Checking : "
    );

    Serial.println(
        url
    );


    http.begin(
        url
    );


    int response =
        http.GET();


    if (response != HTTP_CODE_OK)
    {
        Serial.print(
            "Network Update Check Failed : "
        );

        Serial.println(
            response
        );


        http.end();

        return;
    }


    String result =
        http.getString();


    result.trim();


    http.end();


    Serial.print(
        "Server Response : "
    );

    Serial.println(
        result
    );


    if (result != "UPDATE")
    {
        Serial.println(
            "No Network Update"
        );

        return;
    }


    // =====================================================
    // DOWNLOAD CONFIGURATION
    // =====================================================

    HTTPClient config;


    String configURL =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/network_config";


    Serial.print(
        "Downloading Network Config : "
    );

    Serial.println(
        configURL
    );


    config.begin(
        configURL
    );


    int configResponse =
        config.GET();


    if (
        configResponse !=
        HTTP_CODE_OK
    )
    {
        Serial.println(
            "Network Config Download Failed"
        );


        config.end();

        return;
    }


    String json =
        config.getString();


    config.end();


    Serial.println();
    Serial.println(
        "NETWORK CONFIG RECEIVED:"
    );

    Serial.println(
        json
    );


    // =====================================================
    // PARSE
    // =====================================================

    JsonDocument doc;


    DeserializationError error =
        deserializeJson(
            doc,
            json
        );


    if (error)
    {
        Serial.println(
            "Network JSON Parse Failed"
        );

        return;
    }


    String newMode =
        doc["mode"] | "station";

    String newSSID =
        doc["ssid"] | "";

    String newPassword =
        doc["password"] | "";

    String newIPMode =
        doc["ip_mode"] | "dynamic";

    String newStaticIP =
        doc["static_ip"] | "";

    String newGateway =
        doc["gateway"] | "";

    String newSubnet =
        doc["subnet"] | "";

    String newDNS =
        doc["dns"] | "";

    String newServerIP =
        doc["server"] | "";

    if (newServerIP == "")
    {
        newServerIP =
            doc["server_ip"] |
            FLASK_SERVER_IP;
    }

    String newPort =
        doc["port"] | "5000";

    String newApiPath =
        doc["path"] | "";

    if (newApiPath == "")
    {
        newApiPath =
            doc["api_path"] | "/sensor";
    }


    // =====================================================
    // SAVE
    // =====================================================

    preferences.begin(
        "config",
        false
    );


    preferences.putString(
        "mode",
        newMode
    );

    preferences.putString(
        "ssid",
        newSSID
    );

    preferences.putString(
        "password",
        newPassword
    );

    preferences.putString(
        "ip_mode",
        newIPMode
    );

    preferences.putString(
        "static_ip",
        newStaticIP
    );

    preferences.putString(
        "gateway",
        newGateway
    );

    preferences.putString(
        "subnet",
        newSubnet
    );

    preferences.putString(
        "dns",
        newDNS
    );

    preferences.putString(
        "server",
        newServerIP
    );

    preferences.putString(
        "port",
        newPort
    );

    preferences.putString(
        "path",
        newApiPath
    );


    preferences.end();


    Serial.println();
    Serial.println(
        "Network configuration updated."
    );


    // =====================================================
    // ACK
    // =====================================================

    HTTPClient ack;


    String ackURLNetwork =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/network_ack";


    ack.begin(
        ackURLNetwork
    );


    int ackResponse =
        ack.GET();


    Serial.print(
        "Network ACK Response : "
    );

    Serial.println(
        ackResponse
    );


    ack.end();


    Serial.println(
        "Restarting ESP32 for network changes..."
    );


    delay(1000);


    ESP.restart();
}


// =========================================================
// CHECK SENSOR UPDATE
// =========================================================

void checkSensorUpdate()
{
    if (WiFi.status() != WL_CONNECTED)
        return;


    HTTPClient http;


    String url =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/sensor_update";


    Serial.println();
    Serial.println(
        "========== SENSOR UPDATE =========="
    );


    Serial.print(
        "Checking : "
    );

    Serial.println(
        url
    );


    http.begin(
        url
    );


    int response =
        http.GET();


    if (
        response !=
        HTTP_CODE_OK
    )
    {
        Serial.print(
            "Sensor Update Check Failed : "
        );

        Serial.println(
            response
        );


        http.end();

        return;
    }


    String result =
        http.getString();


    result.trim();


    http.end();


    Serial.print(
        "Server Response : "
    );

    Serial.println(
        result
    );


    if (result != "UPDATE")
    {
        Serial.println(
            "No Sensor Update"
        );

        return;
    }


    // =====================================================
    // DOWNLOAD SENSOR CONFIG
    // =====================================================

    HTTPClient config;


    String configURL =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/sensor_config";


    config.begin(
        configURL
    );


    int configResponse =
        config.GET();


    if (
        configResponse !=
        HTTP_CODE_OK
    )
    {
        Serial.println(
            "Sensor Config Download Failed"
        );


        config.end();

        return;
    }


    String json =
        config.getString();


    config.end();


    Serial.println();
    Serial.println(
        "SENSOR CONFIG RECEIVED:"
    );

    Serial.println(
        json
    );


    // =====================================================
    // PARSE
    // =====================================================

    JsonDocument doc;


    DeserializationError error =
        deserializeJson(
            doc,
            json
        );


    if (error)
    {
        Serial.println(
            "Sensor JSON Parse Failed"
        );

        return;
    }


    String newName =
        doc["sensor_name"] |
        "Sensor";


    String newType =
        doc["sensor_type"] |
        "IR";


    int newPin =
        doc["gpio"] | 4;


    String newPinMode =
        doc["pin_mode"] |
        "Digital Input";


    String newStatus =
        doc["status"] |
        "Enabled";


    // =====================================================
    // SAVE SENSOR CONFIG
    // =====================================================

    preferences.begin(
        "sensor",
        false
    );


    preferences.putString(
        "name",
        newName
    );

    preferences.putString(
        "type",
        newType
    );

    preferences.putInt(
        "pin",
        newPin
    );

    preferences.putString(
        "pin_mode",
        newPinMode
    );

    preferences.putString(
        "status",
        newStatus
    );


    preferences.end();


    // =====================================================
    // APPLY
    // =====================================================

    sensorType =
        newType;


    sensorPin =
        newPin;


    pinMode(
        sensorPin,
        INPUT
    );


    Serial.println();
    Serial.println(
        "Sensor configuration updated."
    );


    Serial.print(
        "Sensor Name : "
    );

    Serial.println(
        newName
    );


    Serial.print(
        "Sensor Type : "
    );

    Serial.println(
        newType
    );


    Serial.print(
        "GPIO        : "
    );

    Serial.println(
        newPin
    );


    Serial.print(
        "Pin Mode    : "
    );

    Serial.println(
        newPinMode
    );


    Serial.print(
        "Status      : "
    );

    Serial.println(
        newStatus
    );


    // =====================================================
    // ACK
    // =====================================================

    HTTPClient ack;


    String ackURLSensor =
        "http://" +
        serverIP +
        ":" +
        serverPort +
        "/sensor_ack";


    ack.begin(
        ackURLSensor
    );


    int ackResponse =
        ack.GET();


    Serial.print(
        "Sensor ACK Response : "
    );

    Serial.println(
        ackResponse
    );


    ack.end();
}