/* Final Project JELLYFISH
   BMP180, TSL2561, and VL5L0X (Temperature, light, and distance!) sensors communicate with MQTT server!
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
#include <FastLED.h> // Library for LEDs
#include <Servo.h> // Library for servo

/* Connections
   TSL2561: SCL to I2C SCL; SDA to I2C SDA; use default ADDR of float, unconnected
   BMP180: SCL to I2C SCL; SDA to I2C SDA
   VL53LOX: SCL to I2C SCL; SDA to I2C SDA
   Sensor I2C addresses don't conflict (reference: https://learn.adafruit.com/i2c-addresses/the-list)
*/

#define DEBUG 1 // Debug mode flag to control sketch behavior for testing (e.g. when true, print additional info to serial)
#define MQTTDEBUG 0 // Debug mode flag to control sketch behavior for testing (e.g. when true, don't connect to MQTT, print additional info to serial)
#define FASTLED_FORCE_SOFTWARE_SPI // This is for FastLED performance on the ESP8266
#define LED_DATA_PIN 4 // Data pin for APA102 (DotStar) LED strip
#define LED_CLOCK_PIN 5 // Clock pin for APA102 (DotStar) LED strip
#define NUM_LEDS 170 // Number of LEDs on APA102 to control
CRGB leds[NUM_LEDS]; // Define array of LEDs for FastLED
#define SERVO_PIN 2  // Pin for movement

// Create sensors
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085); // I2C ADRR 0x77
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345); // I2C ADDR is 0x29
Adafruit_VL53L0X lox = Adafruit_VL53L0X(); // I2C ADDR 0x39

Servo myservo;  // Create servo object for movement

// JELLYFISH STATES
int defaultState; // Flag for lighting LEDs
int predatorDetected; // Flag for predator response
int lightNeeded; // Flag for lighting LEDs
int oceanWarming; // Color change test
int oceanCooling; // Color change test
String lightColor; // Color for LED strip

// Set up MQTT
WiFiClient espClient; // Create ESP client
PubSubClient mqtt(espClient); // Use for MQTT client
char mac[6]; // MQTT needs a unique ID; we're going to use MAC address per brc class example
char messageTemp[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
char messageLight[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
char messageOceanTemp[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
char messageDistance[201]; // Array for messages; size 401, as last character in the array is the NULL character, denoting the end of the array
unsigned long currentMillis, timerOne, timerTwo; // Timers to keep track of messaging intervals for MQTT & sensors

// Set up data types for API data

typedef struct { //here we create a new data type definition, a box to hold other data types
  String ip;    // Slot in structure for each pair
  String ln;
  String lt;
} LocationData;     //then we give our new data structure a name so we can use it in our code

LocationData location; // Create an instance of LocationData

typedef struct { //here we create a new data type definition, a box to hold other data types
  String windspeed;
  String winddir;
  String cloud;
} StormData;     //then we give our new data structure a name so we can use it in our code

StormData incoming; // Create an instance of StormData

typedef struct { //here we create a new data type definition, a box to hold other data types
  String water_temperature;
} OceanData;     //then we give our new data structure a name so we can use it in our code

OceanData ocean; // Create an instance of Ocean

void setup() { // This code runs once
  // Start the serial connection
  Serial.begin(115200);

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

  // Add LEDs
  FastLED.addLeds<APA102, LED_DATA_PIN, LED_CLOCK_PIN, BGR>(leds, NUM_LEDS); // My strip is BGR for some reason

  // Add servo
  myservo.attach(SERVO_PIN);  // attaches the servo on GIO2 to the servo object
}

void loop() {
  // put your main code here, to run repeatedly:

  unsigned long currentMillis = millis(); // Timer

  if (!MQTTDEBUG) { // If not in MQTT debug mode, do this
    mqtt.loop(); // Keep MQTT connection active
    if (!mqtt.connected()) {
      reconnect();
    }
  }

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

      //getMet();
      getNOAA();

      // For proper JSON, we need the "name": "value" pair to be in quotes, so we use internal quotes
      // in the string, which we tell the compiler to ignore by escaping the inner quotes with the '/' character
      sprintf(messageTemp, "{\"Temperature F\":\"%s\"}", pStrTemp); // Write temperature to message
      sprintf(messageLight, "{\"Lux\":\"%s\"}", pStrLight); // Write light value to message
      sprintf(messageOceanTemp, "{\"Water Temperature\":\"%s\"}", ocean.water_temperature.c_str()); // Write wind speed value to message
      sprintf(messageDistance, "{\"Predator Distance\":\"%s\"}", pStrDistance); // Write distance sensor value to message

      mqtt.publish("jellyfish/Temperature", messageTemp); // Publish this message to the Temperature subtopic
      mqtt.publish("jellyfish/Light", messageLight); // Publish this message to the Light subtopic
      mqtt.publish("jellyfish/Predator", messageDistance); // Publish this message to the Light subtopic
      mqtt.publish("jellyfish/WaterTemperature", messageOceanTemp); // Publish this message to the Light subtopic
      timerOne = currentMillis; // Update timer for this loop; used for sensors and MQTT publishing
    }

    // COLOR & STATES

    if (lightNeeded && !oceanWarming) { // If MQTT message says it's dark there is no warming
      defaultState = true;
      if (lightColor == "Yellow") {
        for (int i; i < NUM_LEDS; i++) { // Loop through the LED array
          leds[i] = CRGB::Yellow; // Set LED strip to yellow
          FastLED.show(); // Push to LED
          mqtt.loop(); // Check for incoming status
        }
      }
      else if (lightColor == "Salmon") {
        for (int i; i < NUM_LEDS; i++) { // Loop through the LED array
          leds[i] = CRGB::Salmon; // Set LED strip to orange
          FastLED.show(); // Push to LED
          mqtt.loop(); // Check for incoming status
        }
      }
      else if (lightColor == "Green") {
        for (int i; i < NUM_LEDS; i++) { // Loop through the LED array
          leds[i] = CRGB::YellowGreen; // Set LED strip to yellowgreen
          FastLED.show(); // Push to LED
          mqtt.loop(); // Check for incoming status
        }
      }
      else if (lightColor == "Aqua") {
        for (int i; i < NUM_LEDS; i++) { // Loop through the LED array
          leds[i] = CRGB::Aqua; // Set LED strip to aqua
          FastLED.show(); // Push to LED
          mqtt.loop(); // Check for incoming status
        }
      }
      mqtt.loop(); // Check for incoming status
    }
    else if (lightNeeded && oceanWarming) { // If it's dark and the ocean is too hot, make the lights red
      for (int i; i < NUM_LEDS; i++) { // Loop through the LED array
        leds[i] = CRGB::Red; // Set LED strip to red
        FastLED.show(); // Push to LED
        mqtt.loop(); // Check for incoming status
      }
    }
    else {
      for (int i; i < NUM_LEDS; i++) { // Loop through the LED array
        leds[i] = CRGB::Black; // Set LED strip to black ("turn off")
        FastLED.show(); // Push to LED
        mqtt.loop(); // Check for incoming status
      }
      mqtt.loop(); // Check for incoming status
    }

    if (predatorDetected) { // If the flag is set, move the servo
      int pos;
      for (pos = 0; pos <= 180; pos += 1) // goes from 0 degrees to 180 degrees
      { // in steps of 1 degree
        myservo.write(pos); // tell servo to go to position in variable 'pos'
        delay(15); // waits 15ms for the servo to reach the position
        mqtt.loop(); // Check for incoming status
      }
      for (pos = 180; pos >= 0; pos -= 1) // goes from 180 degrees to 0 degrees
      {
        myservo.write(pos); // tell servo to go to position in variable 'pos'
        delay(15); // waits 15ms for the servo to reach the position
        mqtt.loop(); // Check for incoming status
      }
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
        lightColor = "Yellow";
      }
      else if (luxValue < 50) {
        lightColor = "Salmon";
      }
      else if (luxValue < 150) {
        lightColor = "Green";
      }
      else {
        lightColor = "Aqua";
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
    if (distanceValue < 200) {
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
    if (waterTempValue > 70) {
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
}

// TO GET WEATHER ALERT DATA

String getIP() { // First get the IP address for the jellyfish
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

void getLocation() { // Get the location (latitude and longitude) based on IP
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

void getMet() { // Get the weather for the given latitude and longitude
  HTTPClient theClient; // Create HTTPClient

  String apiCall = "http://api.openweathermap.org/data/2.5/weather?"; // this API defaults to JSON
  String weatherLocation = "lat=" + location.lt + "&lon=" + location.ln; // Call location by GEO coordinates returned from GetGeo(). This requires only weather? with no q= in URL
  apiCall += weatherLocation; // Add the city name (or lat & lon if using that) to the call
  apiCall += "&APPID="; // The URL string needs to include this to take an API key
  apiCall += weather_API_KEY; // Add the API key to the URL string
  apiCall += "&units=imperial"; // US units
  theClient.begin(apiCall); // Make Open Weather API call using formed URL
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

      //Using .dot language, we refer to the variable "weather" which is of
      //type MetData, and place our data into the data structure.

      incoming.windspeed = root["wind"]["speed"].as<String>();
      incoming.winddir = root["wind"]["deg"].as<String>();
      incoming.cloud = root["clouds"]["all"].as<String>();

    } else {
      Serial.println("Something went wrong with connecting to the weather endpoint."); // If not a 200 OK, something needs to be fixed
    }
  }
}

void getNOAA() { // Get the weather for the given latitude and longitude
  HTTPClient theClient; // Create HTTPClient

  String apiCall = "https://tidesandcurrents.noaa.gov/api/datagetter?date=latest&station=9447130&product=water_temperature&datum=STND&units=english&time_zone=lst&application=citizen&format=json"; // ask for JSON

  theClient.begin(apiCall, fingerprint); // Make Open Weather API call using formed URL
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

      //Using .dot language, we refer to the variable "weather" which is of
      //type OceanData, and place our data into the data structure.

      ocean.water_temperature = root["data"][0]["v"].as<String>();

    } else {
      Serial.println("Something went wrong with connecting to the NOAA endpoint."); // If not a 200 OK, something needs to be fixed
    }
  }
  else Serial.println("COULDN'T CONNECT");
}