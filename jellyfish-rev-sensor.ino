/* Final Project JELLYFISH
   BMP180, VL53L0X, and TSL2561 (Temperature, distance, and light!) sensors communicate with MQTT server!
   API data to retrieve sensor IP, Geolocation, and NOAA ocean data
   The module is subscribed to MQTT channels and takes action based on messages
*/

#include "config.h" // config.h contains WiFi and Adafruit IO connection information
#include <Wire.h> // Library for I2C communication (sensors)
#include <Adafruit_Sensor.h> // Unified sensor library (reference: https://learn.adafruit.com/using-the-adafruit-unified-sensor-driver)
#include <Adafruit_BMP085_U.h> // Temperature sensor
#include <Adafruit_TSL2561_U.h> // Light sensor
#include "Adafruit_VL53L0X.h" // Distance sensor
#include <ESP8266WiFi.h> // Libraries for MQTT, API calls, & json
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

/* Connections
   TSL2561: SCL to I2C SCL; SDA to I2C SDA; use default ADDR of float, unconnected
   BMP180: SCL to I2C SCL; SDA to I2C SDA
   VL53LOX: SCL to I2C SCL; SDA to I2C SDA
   Sensor I2C addresses don't conflict (reference: https://learn.adafruit.com/i2c-addresses/the-list)
*/

#define DEBUG 1 // Debug mode flag to control sketch behavior for testing (e.g. when true, print additional info to serial)
#define MQTTDEBUG 0 // Debug mode flag to control sketch behavior for testing (e.g. when true, don't connect to MQTT, print additional info to serial)

// Create sensors
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085); //0x77
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345); //ADDR LOW is 0x29
Adafruit_VL53L0X lox = Adafruit_VL53L0X(); // ADDR 0x39

// JELLYFISH STATES
int predatorDetected; // Flag for predator response
int lightNeeded; // Flag for lighting LEDs
int oceanWarming; // Flag for optimal water temperature (60-77 F)
int idealSalinity; // Flag for optimal salinity (30-34 ppt)
String lightColor; // Color for LED strip
unsigned long startTime; // Timer for LED patterns
int waterFill; // For default LED pattern


// Set up MQTT
WiFiClient espClient; // Create ESP client
PubSubClient mqtt(espClient); // Use for MQTT client
char mac[6]; // MQTT needs a unique ID; we're going to use MAC address per brc class example
char messageTemp[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
char messageLight[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
char messageOceanTemp[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
char messageDistance[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
char messageSalinity[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
unsigned long currentMillis, timerOne, timerTwo; // Timers to keep track of messaging intervals for MQTT & sensors

// Set up data types for API data

typedef struct { //here we create a new data type definition, a box to hold other data types
  String ip;    // Slot in structure for each pair
  String ln;
  String lt;
} LocationData;     //then we give our new data structure a name so we can use it in our code

LocationData location; // Create an instance of LocationData

typedef struct { //here we create a new data type definition, a box to hold other data types
  String water_temperature;
} OceanData;     //then we give our new data structure a name so we can use it in our code

OceanData ocean; // Create an instance of Ocean

void setup() { // This code runs once
  Wire.begin(12, 14); // Set I2C to run on pins 12 and 14 for all sensors, since SPI is using 4/5 for LEDs

  Serial.begin(115200);   // Start the serial connection

  // Get the sensor module's geolocation when setting up (calls getIP())
  setupWiFi();
  //  getLocation();
  //  if (DEBUG) {
  //    Serial.println("The sensor is located at (approximately) ");
  //    Serial.println(location.lt + " latitude by " + location.ln + " longitude.");
  //  }

  // BMP180 Setup
  if (DEBUG) {
    if (DEBUG) { // In testing mode, tell us more about what's going on
      Serial.println("Initializing BMP180 sensor"); Serial.println(""); //
    }
    if (!bmp.begin()) {
      Serial.println("Could not find a valid BMP180 sensor; check your wiring or I2C address!");
      while (1) {}
    }
  }
  displayBMPSensorDetails(); // Print out some information about the BMP sensor

  // TSL2561 Setup
  if (DEBUG) { // In testing mode, tell us more about what's going on
    Serial.println("Initializing TSL2561 sensor"); Serial.println(""); //
  }
  if (!tsl.begin()) { // Initialize TSL; if it fails, print message
    Serial.print("No TSL2561 detected;check your wiring or I2C address!");
    while (1);
  }
  configureTSLSensor(); // Set up TSL sensor parameters
  displayTSLSensorDetails(); // Print out some information about the TSL sensor

  // VL53LO Setup
  if (DEBUG) { // In testing mode, tell us more about what's going on
    Serial.println("Initializing VL53LO sensor"); Serial.println(""); //
  }
  if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    while (1);
  }

  // MQTT setup
  if (!MQTTDEBUG) { // If not in MQTT debug mode, connect
    MQTTSetup();
  }
}

void loop() {
  unsigned long currentMillis = millis(); // Timer

  if (!mqtt.connected()) { // Connect to MQTT
    reconnect();
  }
  mqtt.loop(); // Keep MQTT connection active

  // SENSORS & COMMUNICATIONS

  if (currentMillis - timerOne > 5000) { // Timer for sensors, do this every 5 seconds

    if (DEBUG) { // If testing mode
      Serial.println(); // Line for readability
      Serial.println("Timer values: ");
      Serial.println(currentMillis); // Print current time at loop
      Serial.println(timerOne); // Print time from timer 1
      Serial.println(); // Line for readability
    }

    // For MQTT message
    char str_temp[8]; // Temp arrays of size 8 to hold "XXXX.XX" + the terminating character
    char str_light[10];
    char str_distance[10];
    char str_salinity[4];

    sensors_event_t event;

    bmp.getEvent(&event); // Get temp reading from BMP
    if (event.pressure) {
      float celsius;
      bmp.getTemperature(&celsius); // Get the temperature reading from the event
      float fahrenheit = (celsius * 1.8) + 32; // Convert the temperature to fahrenheit

      if (DEBUG) {
        Serial.print("Celsius: "); // Print the temperature value in C
        Serial.print(celsius);
        Serial.println("C");

        Serial.print("Fahrenheit: "); // Print the temperature value in F
        Serial.print(fahrenheit);
        Serial.println("F");
      }

      dtostrf(fahrenheit, 8, 2, str_temp); // Write temperature value to str_temp for use in MQTT message
      char* pStrTemp = str_temp; // Pointer to str_temp
      while (*pStrTemp == ' ') pStrTemp++; // Trim spaces

      tsl.getEvent(&event); // Get lux reading from TSL

      if (DEBUG) {
        if (event.light) {
          Serial.print("Lux: ");
          Serial.println(event.light);
        }
        else {
          Serial.println("Sensor overload or other error!");
        }
      }

      dtostrf(event.light, 10, 2, str_light); // Write light value to str_light for use in MQTT message
      char* pStrLight = str_light; // Pointer to str_light
      while (*pStrLight == ' ') pStrLight++; // Trim spaces

      // Distance
      VL53L0X_RangingMeasurementData_t measure;

      Serial.print("Reading a measurement... ");
      lox.rangingTest(&measure, false); // pass in 'true' to get debug data printout!

      if (measure.RangeStatus != 4) {  // phase failures have incorrect data
        Serial.print("Distance (mm): "); Serial.println(measure.RangeMilliMeter);
      } else {
        Serial.println("Out of range or other error!");
      }

      dtostrf(measure.RangeMilliMeter, 10, 2, str_distance); // Write distance value to str_distance for use in MQTT message
      char* pStrDistance = str_distance; // Pointer to str_light
      while (*pStrDistance == ' ') pStrDistance++; // Trim spaces

      getNOAA();

      // For proper JSON, we need the "name": "value" pair to be in quotes, so we use internal quotes
      // in the string, which we tell the compiler to ignore by escaping the inner quotes with the '/' character
      sprintf(messageTemp, "{\"Temperature F\":\"%s\"}", pStrTemp); // Write temperature to message
      sprintf(messageLight, "{\"Lux\":\"%s\"}", pStrLight); // Write light value to message
      sprintf(messageOceanTemp, "{\"Water Temperature\":\"%s\"}", ocean.water_temperature.c_str()); // Write wind speed value to message
      sprintf(messageDistance, "{\"Predator Distance\":\"%s\"}", pStrDistance); // Write distance sensor value to message
      sprintf(messageSalinity, "{\"Salinity\":\"%s\"}", "32"); // Write hard-coded salinity value to message (couldn't find live API)

      mqtt.publish("jellyfish/Temperature", messageTemp); // Publish this message to the Temperature subtopic
      mqtt.publish("jellyfish/Light", messageLight); // Publish this message to the Light subtopic
      mqtt.publish("jellyfish/Predator", messageDistance); // Publish this message to the Predator subtopic
      mqtt.publish("jellyfish/WaterTemperature", messageOceanTemp); // Publish this message to the Water Temperature subtopic
      mqtt.publish("jellyfish/Salinity", messageSalinity); // Publish this message to the Salinity subtopic

      timerOne = currentMillis; // Update timer for this loop; used for sensors and MQTT publishing
    }
  }
}

// SENSOR FUNCTIONS

void configureTSLSensor(void) { // Code modified from Adafruit TSL2561 example
  tsl.enableAutoRange(true); // Auto-gain ... switches automatically between 1x and 16x */

  //Changing the integration time gives you better sensor resolution (402ms = 16-bit data)
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  // Mid-range resolution and speed

  if (DEBUG) { // If debug, print sensor settings
    Serial.println("------------------------------------");
    Serial.print  ("Gain:         "); Serial.println("Auto");
    Serial.print  ("Timing:       "); Serial.println("101 ms");
    Serial.println("------------------------------------");
  }
}

void displayTSLSensorDetails(void) { // Code modified from Adafruit TSL2561 example
  sensor_t sensor;
  tsl.getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.print  ("Sensor:       "); Serial.println(sensor.name);
  Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" lux");
  Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" lux");
  Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" lux");
  Serial.println("------------------------------------");
  Serial.println("");
  delay(500);
}

void displayBMPSensorDetails(void)
{
  sensor_t sensor;
  bmp.getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.print  ("Sensor:       "); Serial.println(sensor.name);
  Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" hPa");
  Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" hPa");
  Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" hPa");
  Serial.println("------------------------------------");
  Serial.println("");
  delay(500);
}

// COMMUNICATION FUNCTIONS

void MQTTSetup() { // Set up MQTT - attribution: brc 2018
  //setupWiFi();
  mqtt.setServer(mqtt_server, 1883); // Server name, port
  mqtt.setCallback(callback); //register the callback function
  timerOne = millis(); // Starting counts
}

void setupWiFi() { // Set up WiFi - attribution: brc 2018
  delay(10); // Wait 10 ms
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID); // Print to serial
  WiFi.begin(WIFI_SSID, WIFI_PASS); // Connect to WiFi
  while (WiFi.status() != WL_CONNECTED) { // Print . for every half second while not yet connected
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");  //get the unique MAC address to use as MQTT client ID, a 'truly' unique ID.
  WiFi.macAddress((byte*)mac); // .macAddress returns byte array : 6 bytes representing the MAC address of your shield
  Serial.println(mac);  // Print address to serial
}

// Connect/Reconnect to MQTT server if disconnect - attribution: brc 2018
void reconnect() {
  // Loop until we're reconnected
  while (!mqtt.connected()) { // When disconnected
    Serial.print("Attempting MQTT connection...");
    Serial.println(mac);
    Serial.println(mqtt_user);
    Serial.println(mqtt_pass);
    if (mqtt.connect(mac, mqtt_user, mqtt_pass)) { // Send ID of MAC address, credentials
      Serial.println("connected");
      mqtt.subscribe("jellyfish/+"); //we are subscribing to 'joeyData' and all subtopics below that topic
    } else { // If it doesn't work, wait 5 seconds and try again to reconncet
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// SUBSCRIBE TO MQTT

void callback(char* topic, byte * payload, unsigned int length) { // Attach listener to topics - attribution: brc 2018
  Serial.println();
  Serial.print("Message arrived [");
  Serial.print(topic); //'topic' refers to the incoming topic name, the 1st argument of the callback function
  Serial.println("] ");

  DynamicJsonBuffer jsonBuffer; // Create a JSON buffer
  JsonObject& root = jsonBuffer.parseObject(payload); // Parse the payload

  if (!root.success()) { // If it doesn't parse, say so!
    Serial.println("parseObject() failed; Are you sure this message is properly JSON formatted?");
    return;
  }

  if (strcmp(topic, "jellyfish/Temperature") == 0) { // If new temperature data
    Serial.println("Temperature updated!");
    Serial.println(); // Line for readability
    if (DEBUG) { // If in debug mode, print info
      root.printTo(Serial); // The parsed message
      Serial.println(); // Line for readability
    }
  }
  else if (strcmp(topic, "jellyfish/Light") == 0) { // If new light data
    int luxValue = root["Lux"].as<int>(); // read the value from the parsed string and set it to luxValue
    if (luxValue > 200) {
      // Turn LED off if bright by setting light flag to false
      lightNeeded = 0;
      lightColor = "Black";
      if (DEBUG) { // If in debug mode, print info
        Serial.print("LIGHT FLAG: ");
        Serial.println(lightNeeded);
        Serial.println(); // Line for readability
      }
    }
    else {
      // Turn LED on if dark by setting light flag to true
      lightNeeded = 1;
      if (luxValue < 20) { // Change colors based on amount of light! (arbitrary values based on what I could test at home)
        lightColor = "Orange";
      }
      else if (luxValue < 50) {
        lightColor = "Fuchsia";
      }
      else if (luxValue < 150) {
        lightColor = "Green";
      }
      else {
        lightColor = "Indigo";
      }
      if (DEBUG) { // If in debug mode, print info
        Serial.print("LIGHT FLAG: ");
        Serial.println(lightNeeded);
        Serial.print("COLOR REQUESTED: ");
        Serial.println(lightColor);
        Serial.println(); // Line for readability
      }
    }
    if (DEBUG) { // If in debug mode, print info
      root.printTo(Serial); // The parsed message
      Serial.println(); // Line for readability
    }
  }
  else if (strcmp(topic, "jellyfish/Predator") == 0) { // If new water temperature data
    Serial.println("Incoming predator info!");
    int distanceValue = root["Predator Distance"].as<int>(); // read the value from the parsed string and set it to luxValue
    if (distanceValue < 50) {
      // Turn LED off if bright by setting light flag to false
      predatorDetected = 1;
      if (DEBUG) { // If in debug mode, print info
        Serial.print("PREDATOR DETECTION FLAG: ");
        Serial.println(predatorDetected);
        Serial.println(); // Line for readability
      }
    } else {
      predatorDetected = 0;
      if (DEBUG) { // If in debug mode, print info
        Serial.print("PREDATOR DETECTION FLAG: ");
        Serial.println(predatorDetected);
        Serial.println(); // Line for readability
      }
    }
    if (DEBUG) { // If in debug mode, print info
      root.printTo(Serial); // The parsed message
      Serial.println(); // Line for readability
    }
  }
  else if (strcmp(topic, "jellyfish/WaterTemperature") == 0) { // If new water temperature data
    Serial.println("Incoming water temperature info!");
    int waterTempValue = root["Water Temperature"].as<int>(); // read the value from the parsed string and set it to luxValue
    if (waterTempValue > 80) {
      // Turn LED off if bright by setting light flag to false
      oceanWarming = 1;
      if (DEBUG) { // If in debug mode, print info
        Serial.print("WATER TEMPERATURE FLAG: ");
        Serial.println(oceanWarming);
        Serial.println(); // Line for readability
      }
    } else {
      oceanWarming = 0;
      if (DEBUG) { // If in debug mode, print info
        Serial.print("WATER TEMPERATURE FLAG: ");
        Serial.println(oceanWarming);
        Serial.println(); // Line for readability
      }
    }
    if (DEBUG) { // If in debug mode, print info
      root.printTo(Serial); // The parsed message
      Serial.println(); // Line for readability
    }
  }
  else if (strcmp(topic, "jellyfish/Salinity") == 0) { // If new water temperature data
    Serial.println("Incoming salinity info!");
    int salinity = root["Salinity"].as<int>(); // read the value from the parsed string and set it to luxValue
    if (30 <= salinity && salinity <= 35) {
      // Turn LED off if bright by setting light flag to false
      idealSalinity = 1;
      if (DEBUG) { // If in debug mode, print info
        Serial.print("SALINITY FLAG: ");
        Serial.println(idealSalinity);
        Serial.println(); // Line for readability
      }
    } else {
      idealSalinity = 0;
      if (DEBUG) { // If in debug mode, print info
        Serial.print("SALINITY FLAG: ");
        Serial.println(idealSalinity);
        Serial.println(); // Line for readability
      }
    }
    if (DEBUG) { // If in debug mode, print info
      root.printTo(Serial); // The parsed message
      Serial.println(); // Line for readability
    }
  }
}

// TO GET API DATA

String getIP() { // First get the IP address for the jellyfish [NOT CURRENTLY USING; ONLY NEEDED FOR GEOIP]
  HTTPClient theClient;
  String ipAddress;

  theClient.begin("http://api.ipify.org/?format=json"); // ask fror this as JSON
  int httpCode = theClient.GET();

  if (httpCode > 0) {
    if (httpCode == 200) {

      DynamicJsonBuffer jsonBuffer;

      String payload = theClient.getString();
      JsonObject& root = jsonBuffer.parse(payload);
      ipAddress = root["ip"].as<String>();
      Serial.println(ipAddress);

    } else {
      Serial.println("Something went wrong with connecting to the IP endpoint.");
      return "error";
    }
  }
  return ipAddress;
}

void getLocation() { // Get the location (latitude and longitude) based on IP  [NOT CURRENTLY USING THIS BECAUSE SERVICE IS $, NEED TO FIND NEW ONE]
  HTTPClient theClient;
  theClient.begin("http://api.ipstack.com/" + getIP() + "?access_key=" + geoAccessKey); // default returns JSON
  int httpCode = theClient.GET();

  if (httpCode > 0) {
    if (httpCode == 200) {

      DynamicJsonBuffer jsonBuffer;

      String payload = theClient.getString();
      JsonObject& root = jsonBuffer.parse(payload);

      // Test if parsing succeeds.
      if (!root.success()) {
        Serial.println("parseObject() failed");
        return;
      }

      //Some debugging lines below:
      Serial.println(payload);
      root.printTo(Serial);

      //Using .dot language, we refer to the variable "location" which is of
      //type LocationData, and place our data into the data structure.

      location.ip = root["ip"].as<String>(); // Cast as string
      location.lt = root["latitude"].as<String>(); // We need the sensor latitude and longitude to pass to Weather Service
      location.ln = root["longitude"].as<String>();

    } else {
      Serial.println("Something went wrong with connecting to the Geo Location endpoint.");
    }
  }
}

void getNOAA() { // Get NOAA data for the given location [GEO CURRENTLY NOT USED; HARD CODED WEATHER STATION IN PUGET SOUND]
  HTTPClient theClient; // Create HTTPClient

  String apiCall = "https://tidesandcurrents.noaa.gov/api/datagetter?date=latest&station=9447130&product=water_temperature&datum=STND&units=english&time_zone=lst&application=citizen&format=json"; // ask for JSON

  theClient.begin(apiCall, fingerprintNOAA); // Make Open Weather API call using formed URL
  int httpCode = theClient.GET(); // Make a get request and store resulting HTTP code as int

  if (httpCode > 0) { // if valid HTTP code returned
    if (httpCode == 200) { // 200 OK

      DynamicJsonBuffer jsonBuffer; // Create buffer

      String payload = theClient.getString(); // Store data returned HTTPClient into payload
      JsonObject& root = jsonBuffer.parse(payload); // Parse payload

      // Test if parsing succeeds. Tell us if failed.
      if (!root.success()) {
        Serial.println("parseObject() failed");
        return;
      }

      //Some debugging lines below:
      //Serial.println(payload);
      //root.printTo(Serial);

      ocean.water_temperature = root["data"][0]["v"].as<String>(); // Read results & save

    } else {
      Serial.println("Something went wrong with connecting to the NOAA endpoint."); // If not a 200 OK, something needs to be fixed
    }
  }
  else Serial.println("COULDN'T CONNECT");
}
