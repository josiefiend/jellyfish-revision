/* Final Project DON'T BE JELLY - JELLYFISH
   The module is subscribed to MQTT channels and takes action based on messages
*/

#include "config.h" // config.h contains WiFi and Adafruit IO connection information
#include <ESP8266WiFi.h> // Libraries for MQTT, API calls, & json
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FastLED.h> // Library for LEDs
#include <Servo.h> // Library for servo

#define DEBUG 1 // Debug mode flag to control sketch behavior for testing (e.g. when true, print additional info to serial)
#define MQTTDEBUG 0 // Debug mode flag to control sketch behavior for testing (e.g. when true, don't connect to MQTT, print additional info to serial)
#define LED_DATA_PIN 4 // Data pin for APA102 (DotStar) LED strip
#define LED_CLK_PIN 5 // Clock pin for APA102 (DotStar) LED strip
#define NUM_LEDS 60 // Number of LEDs on APA102 to control
CRGB leds[NUM_LEDS]; // Define array of LEDs for FastLED
#define SERVO_PIN 2  // Pin for movement
#define BRIGHTNESS 200  // Brightness

Servo myservo;  // Create servo object for movement

// JELLYFISH STATES
int predatorDetected; // Flag for predator response
int lightNeeded; // Flag for lighting LEDs
int oceanWarming; // Flag for optimal water temperature (60-77 F)
int idealSalinity; // Flag for optimal salinity (30-34 ppt)
String lightColor; // Color for LED strip - background
String dotColor; // Color for LED strip - movement
unsigned long startTime; // Timer for LED patterns
int waterFill; // For default LED pattern

// Set up MQTT
WiFiClient espClient; // Create ESP client
PubSubClient mqtt(espClient); // Use for MQTT client
char mac[6]; // MQTT needs a unique ID; we're going to use MAC address per brc class example
unsigned long currentMillis, timerOne, timerTwo; // Timers to keep track of messaging intervals for MQTT & sensors

void setup() { // This code runs once
  Serial.begin(115200);   // Start the serial connection
  setupWiFi();
  //  getLocation();
  //  if (DEBUG) {
  //    Serial.println("The sensor is located at (approximately) ");
  //    Serial.println(location.lt + " latitude by " + location.ln + " longitude.");
  //  }

  // MQTT setup
  if (!MQTTDEBUG) { // If not in MQTT debug mode, connect
    MQTTSetup();
  }

  // Add LEDs
  FastLED.addLeds<APA102, LED_DATA_PIN, LED_CLK_PIN, BGR>(leds, NUM_LEDS);
  startTime = millis(); // Timer for light patterns

  // Add servo
  myservo.attach(SERVO_PIN);

}

void loop() {
  unsigned long currentMillis = millis(); // Timer
  int salinity = 34; // Mocked out because I don't have a data source but am going to pretend via MQTT

  if (!mqtt.connected()) { // Connect to MQTT
    reconnect();
  }
  mqtt.loop(); // Keep MQTT connection active

  // LED BEHAVIORS

  if (lightNeeded && !oceanWarming) { // If MQTT message says it's dark but it's calm, turn on the light with fun color!
    defaultLED();
  }
  else if (lightNeeded && oceanWarming) { // If it's dark and too hot, show distress
    traverseLED();
  }
  else {
    for (int i = 0; i < NUM_LEDS; i++) { // Loop through the LED array
      leds[i] = CRGB::Black; // Set LED strip to black ("turn off")
    }
  }
  mqtt.loop(); // Check for incoming status
  FastLED.show(); // Push to LED

  // MOTION BEHAVIORS

  if (predatorDetected) { // If the flag is set, move the servo
    predatorLED(); // Run predator pattern
    moveServo(); // For continuous rotation servo
    // FOR MICROSERVO
    //    int pos;
    //    for (pos = 0; pos <= 180; pos += 1) // goes from 0 degrees to 180 degrees
    //    { // in steps of 1 degree
    //      myservo.write(pos); // tell servo to go to position in variable 'pos'
    //      mqtt.loop(); // Check for incoming status
    //    }
    //    for (pos = 180; pos >= 0; pos -= 1) // goes from 180 degrees to 0 degrees
    //    {
    //      myservo.write(pos); // tell servo to go to position in variable 'pos'
    //      mqtt.loop(); // Check for incoming status
    //    }
  } else myservo.writeMicroseconds(1500);     //stop

  if (!idealSalinity) {
     moveServo(); // For continuous rotation servo
  }
  // FOR MICROSETVO
  //    int pos;
  //    for (pos = 0; pos <= 180; pos += 1) // goes from 0 degrees to 180 degrees
  //    { // in steps of 1 degree
  //      myservo.write(pos); // tell servo to go to position in variable 'pos'
  //      mqtt.loop(); // Check for incoming status
  //    }
  //    for (pos = 180; pos >= 0; pos -= 1) // goes from 180 degrees to 0 degrees
  //    {
  //      myservo.write(pos); // tell servo to go to position in variable 'pos'
  //      mqtt.loop(); // Check for incoming status
  //    }
  //  }
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
    } else { // If it doesn't work, wait 5 seconds and try again to reconnect
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
    int temperature = root["Temperature F"].as<int>(); // read the value from the parsed string and set it to luxValue
    if (temperature < 32) { // Change colors based on external temperature! (arbitrary values based on what I could test at home)
      dotColor = "Snow";
    }
    else if (temperature < 50) {
      dotColor = "Yellow";
    }
    else if (temperature < 70) {
      dotColor = "PaleGreen";
    }
    else {
      dotColor = "Aqua";
    }
    if (DEBUG) { // If in debug mode, print info
      Serial.print("DOT COLOR REQUESTED: ");
      Serial.println(dotColor);
      Serial.println(); // Line for readability
    }
    if (DEBUG) { // If in debug mode, print info
      root.printTo(Serial); // The parsed message
      Serial.println(); // Line for readability
    }
  }
  else if (strcmp(topic, "jellyfish/Light") == 0) { // If new light data
    int luxValue = root["Lux"].as<int>(); // read the value from the parsed string and set it to luxValue
    if (luxValue > 200) {
      // Turn LED off if brig`ht by setting light flag to false
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
  else if (strcmp(topic, "jellyfish/Predator") == 0) { // If new predator data
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
      // Set oceanWarming flag to true to control action
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
  else if (strcmp(topic, "jellyfish/Salinity") == 0) { // If new salinty data
    Serial.println("Incoming salinity info!");
    int salinity = root["Salinity"].as<int>(); // read the value from the parsed string and set it to luxValue
    if (30 <= salinity) {
      if (salinity <= 35) {
        // Set idealSalinity flag to true to control action
        idealSalinity = 1;
        if (DEBUG) { // If in debug mode, print info
          Serial.print("SALINITY FLAG: ");
          Serial.println(idealSalinity);
          Serial.println(); // Line for readability
        }
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

// LED PATTERNS

void defaultLED() { // This is my default Jellyfish behavior; it loosely simulates water filling
  for (int i = 0; i < NUM_LEDS; i++) { // Loop through the LED array
    if (lightColor == "Orange") {
      leds[i] = CRGB::OrangeRed; // Choose base color
    }
    else if (lightColor == "Fuchsia") {
      leds[i] = CRGB::Fuchsia; // Choose base color
    }
    else if (lightColor == "Green") {
      leds[i] = CRGB::Green; // Choose base color
    }
    else if (lightColor == "Indigo") {
      leds[i] = CRGB::Indigo; // Choose base color
    }
  }
  unsigned long timer = millis() - startTime;
  int millisPerLight = 3000 / NUM_LEDS; // 3000 is an arbitrary choice
  if (timer < 3000 - waterFill * millisPerLight) {
    if (dotColor == "Snow") {
      leds[timer / millisPerLight] = CRGB::Snow; // Choose base color
    }
    else if (dotColor == "Yellow") {
      leds[timer / millisPerLight] = CRGB::Yellow; // Choose base color
    }
    else if (dotColor == "PaleGreen") {
      leds[timer / millisPerLight] = CRGB::PaleGreen; // Choose base color
    }
    else if (dotColor == "Aqua") {
      leds[timer / millisPerLight] = CRGB::Aqua; // Choose base color
    }
  }
  else {
    startTime = millis(); // Reset timer
    waterFill++;
    if (waterFill > NUM_LEDS / 2) { // Set to fill halfway up the strip (just for aesthetics); can go up to NUM_LEDS - 1
      waterFill = 0;
    }
  }
  for (int i = 0; i < waterFill; i++) {
    if (dotColor == "Snow") {
      leds[NUM_LEDS - i - 1] = CRGB::Snow; // Choose base color
    }
    else if (dotColor == "Yellow") {
      leds[NUM_LEDS - i - 1] = CRGB::Yellow; // Choose base color
    }
    else if (dotColor == "PaleGreen") {
      leds[NUM_LEDS - i - 1] = CRGB::PaleGreen; // Choose base color
    }
    else if (dotColor == "Aqua") {
      leds[NUM_LEDS - i - 1] = CRGB::Aqua; // Choose base color
    }
  }
  FastLED.show();
}

void traverseLED() { // This pattern just moves a dot
  for (int i = 0; i < NUM_LEDS; i++) { // Loop through the LED array
    leds[i] = CRGB::PowderBlue; // Set LED strip background to pale blue
  }
  unsigned long timer = millis() - startTime;
  int millisPerLight = 5000 / NUM_LEDS;
  if (timer < 5000) {
    leds[timer / millisPerLight] = CRGB::Orange;
  }
  else {
    startTime = millis(); // Reset timer
  }
  FastLED.show();
}

void predatorLED() {
  unsigned long timer = millis() - startTime;
  if (timer < 3000) {
    FastLED.showColor(CRGB::Yellow);
  }
  else {
    defaultLED();
  }
}

// SERVO

void moveServo() { // This is for the continuous rotation Servo - added for Maker Faire Day 2
  unsigned long timer = millis() - startTime;
  if (timer < 1500) {
    myservo.writeMicroseconds(3000);     // Turn
  }
  else if (timer < 3000) {
    myservo.writeMicroseconds(0);     // Reverse
  }
}
