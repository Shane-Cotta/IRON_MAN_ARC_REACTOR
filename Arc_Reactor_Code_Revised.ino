#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>  // Include the DNSServer library
#include <time.h>
#include "Adafruit_NeoPixel.h"

// -------------------------- Configuration --------------------------

// Pin and NeoPixel configuration
#define NEOPIXEL_PIN    17          // GPIO pin connected to the NeoPixel ring
#define NUMPIXELS       35          // Number of NeoPixels in the ring

// LED Colors
#define COLOR_BLUE      0, 20, 255   // RGB for blue background
#define COLOR_WHITE     250, 250, 250 // RGB for white (chase and flash)
#define COLOR_RED       255, 0, 0    // RGB for red (Wi-Fi lost)
#define COLOR_GREEN     0, 255, 0    // RGB for green (AP mode)

// Preferences Namespace
#define PREF_NAMESPACE  "settings"

// Web Server Port
#define SERVER_PORT     80

// Timezone Configuration
#define TIMEZONE        "PST8PDT,M3.2.0,M11.1.0" // Pacific Time with DST

// DNS Server Configuration
const byte DNS_PORT = 53;

// -------------------------- Global Variables --------------------------

// Preferences library instance
Preferences preferences;

// Web server instance
WebServer server(SERVER_PORT);

// DNS server instance
DNSServer dnsServer;

// NeoPixel instance
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Configuration Variables (Default Values)
String ssid = "";
String password = "";
int chaseSpeed = 50;            // milliseconds per chase step
int chaseInterval = 5;          // minutes between chase effects
int flashInterval = 15;         // minutes between flash effects
int led_ring_brightness = 40;   // Normal brightness
int led_ring_brightness_flash = 250;  // Flash brightness

// LED Control Variables
bool chaseActive = false;
unsigned long previousChaseMillis = 0;
int chasePixel = 0;

bool flashActive = false;
unsigned long lastFlashStartMillis = 0;
const unsigned long flashDuration = 2000; // milliseconds

// Wi-Fi Monitoring Variables
bool wifiConnected = false;
unsigned long previousWifiCheckMillis = 0;
const unsigned long wifiCheckInterval = 5000; // milliseconds

// Wi-Fi Lost Indication Variables
bool wifiLostActive = false;
unsigned long previousWifiLostMillis = 0;
const unsigned long wifiLostBlinkInterval = 1000; // milliseconds
bool redOn = false;

// AP Mode Blinking Green LEDs Variables
unsigned long previousBlinkMillis = 0;
const unsigned long blinkInterval = 500; // milliseconds
bool greenOn = false;

// -------------------------- Function Prototypes --------------------------

void setup();
void loop();

void startAPMode();
void connectToWiFi();
void handleRoot();
void handleSave();
void initializeTime();

void blue_light();
void flash_cuckoo();
void blink_green();

// -------------------------- Setup Function --------------------------

void setup() {
  // Initialize Serial for debugging
  Serial.begin(115200);
  Serial.println("\nStarting ESP32 Configuration...");

  // Initialize NeoPixel
  pixels.begin();
  pixels.setBrightness(led_ring_brightness);
  blue_light();

  // Configure additional GPIO pins
  pinMode(25, OUTPUT);
  pinMode(26, OUTPUT);
  digitalWrite(25, HIGH);
  digitalWrite(26, HIGH);

  // Initialize Preferences
  preferences.begin(PREF_NAMESPACE, false);

  // Retrieve stored settings
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  chaseSpeed = preferences.getInt("chaseSpeed", 50);
  chaseInterval = preferences.getInt("chaseInterval", 5);
  flashInterval = preferences.getInt("flashInterval", 15);
  led_ring_brightness = preferences.getInt("brightness", 40);
  led_ring_brightness_flash = preferences.getInt("flashBrightness", 250);

  // Apply brightness settings
  pixels.setBrightness(led_ring_brightness);
  blue_light();

  // Attempt to connect to Wi-Fi if credentials are stored
  if (ssid != "" && password != "") {
    connectToWiFi();
  } else {
    // Start in AP mode if no credentials are stored
    startAPMode();
  }
}

// -------------------------- Main Loop --------------------------

void loop() {
  // Handle web server
  server.handleClient();

  unsigned long currentMillis = millis();

  // Check Wi-Fi mode
  if (WiFi.getMode() == WIFI_AP) {
    // Process DNS requests
    dnsServer.processNextRequest();

    // Handle blinking green LEDs
    blink_green();

    return; // Skip rest of loop
  }

  // Check Wi-Fi connection status periodically
  if (currentMillis - previousWifiCheckMillis >= wifiCheckInterval) {
    previousWifiCheckMillis = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      if (!wifiConnected) {
        // Wi-Fi reconnected
        wifiConnected = true;
        Serial.println("Wi-Fi Reconnected");
        wifiLostActive = false;
        redOn = false;
        blue_light(); // Restore blue background
        initializeTime();
      }
    } else {
      if (wifiConnected) {
        // Wi-Fi disconnected
        wifiConnected = false;
        Serial.println("Wi-Fi Disconnected");
        wifiLostActive = true;
        previousWifiLostMillis = currentMillis;
      }
      // Attempt to reconnect if credentials are available
      if (ssid != "" && password != "") {
        Serial.println("Attempting to reconnect to Wi-Fi...");
        WiFi.disconnect();
        WiFi.begin(ssid.c_str(), password.c_str());
      }
    }
  }

  // Handle Wi-Fi lost indication (blinking red)
  if (!wifiConnected) {
    // Wi-Fi is not connected, flash red LEDs every second
    if (wifiLostActive && (currentMillis - previousWifiLostMillis >= wifiLostBlinkInterval)) {
      previousWifiLostMillis = currentMillis;
      if (redOn) {
        pixels.clear();
        pixels.show();
        redOn = false;
      } else {
        pixels.setBrightness(led_ring_brightness_flash);
        for (int i = 0; i < NUMPIXELS; i++) {
          pixels.setPixelColor(i, pixels.Color(COLOR_RED));
        }
        pixels.show();
        redOn = true;
      }
    }
    return; // Skip the rest of the loop until Wi-Fi is connected
  }

  // Proceed with normal operation when Wi-Fi is connected

  // Update the time
  time_t now = time(nullptr);
  struct tm* localTime = localtime(&now);

  // Get current minute and second
  int currentMinute = localTime->tm_min;
  int currentSecond = localTime->tm_sec;

  // Start the chase effect at specified intervals
  if (currentMinute % chaseInterval == 0 && !chaseActive && currentSecond == 0) {
    chaseActive = true;
    chasePixel = 0;
    previousChaseMillis = currentMillis;
  }

  // Update the chase effect
  if (chaseActive && (currentMillis - previousChaseMillis >= chaseSpeed)) {
    previousChaseMillis += chaseSpeed;

    // Reset the previous pixel to blue
    int previousPixel = (chasePixel - 1 + NUMPIXELS) % NUMPIXELS;
    pixels.setPixelColor(previousPixel, pixels.Color(COLOR_BLUE));

    // Set the current pixel to white
    pixels.setPixelColor(chasePixel, pixels.Color(COLOR_WHITE));
    pixels.show();

    // Move to the next pixel
    chasePixel++;

    if (chasePixel >= NUMPIXELS) {
      // Completed one full rotation
      chaseActive = false;
      pixels.setPixelColor(chasePixel - 1, pixels.Color(COLOR_BLUE));
      pixels.show();
    }
  }

  // Start the flash effect at specified intervals
  if (currentMinute % flashInterval == 0 && currentSecond == 0 && !flashActive) {
    flashActive = true;
    lastFlashStartMillis = currentMillis;
    flash_cuckoo();
  }

  // Handle the flash effect duration
  if (flashActive) {
    if (currentMillis - lastFlashStartMillis >= flashDuration) {
      flashActive = false;
      if (!chaseActive) {
        blue_light();
      }
    }
  }
}

// -------------------------- Wi-Fi Connection Functions --------------------------

// Connect to Wi-Fi using stored credentials
void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long wifiConnectStart = millis();
  const unsigned long wifiConnectTimeout = 20000; // 20 seconds timeout

  while (WiFi.status() != WL_CONNECTED && millis() - wifiConnectStart < wifiConnectTimeout) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWi-Fi connected");
    initializeTime();

    // Start the web server
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
    Serial.println("HTTP server started in STA mode");
  } else {
    wifiConnected = false;
    Serial.println("\nFailed to connect to Wi-Fi");
    wifiLostActive = true;
    previousWifiLostMillis = millis();
    startAPMode();
  }
}

// Start Access Point mode for initial configuration
void startAPMode() {
  Serial.println("Starting Access Point for configuration...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_Config", "configpassword"); // Change SSID and password as desired
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(apIP);

  // Start the DNS server and redirect all domains to the AP IP
  dnsServer.start(DNS_PORT, "*", apIP);

  // Start the web server
  server.onNotFound([apIP]() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("HTTP server started in AP mode");
}

// -------------------------- Web Server Handlers --------------------------

// Serve the configuration HTML page
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>ESP32 Configuration</title>";
  html += "<style>body { font-family: Arial, sans-serif; margin: 20px; }";
  html += "input[type='text'], input[type='password'], input[type='number'] { width: 100%; padding: 8px; margin: 4px 0; }";
  html += "input[type='submit'] { padding: 10px 20px; }</style></head><body>";
  html += "<h1>ESP32 Configuration</h1>";
  html += "<form action=\"/save\" method=\"POST\">";
  html += "Wi-Fi SSID:<br><input type=\"text\" name=\"ssid\" value=\"" + ssid + "\"><br>";
  html += "Wi-Fi Password:<br><input type=\"password\" name=\"password\" value=\"" + password + "\"><br>";
  html += "Chase Speed (ms per step):<br><input type=\"number\" name=\"chaseSpeed\" value=\"" + String(chaseSpeed) + "\"><br>";
  html += "Chase Interval (minutes):<br><input type=\"number\" name=\"chaseInterval\" value=\"" + String(chaseInterval) + "\"><br>";
  html += "Flash Interval (minutes):<br><input type=\"number\" name=\"flashInterval\" value=\"" + String(flashInterval) + "\"><br>";
  html += "LED Brightness (0-255):<br><input type=\"number\" name=\"brightness\" value=\"" + String(led_ring_brightness) + "\" min=\"0\" max=\"255\"><br>";
  html += "Flash Brightness (0-255):<br><input type=\"number\" name=\"flashBrightness\" value=\"" + String(led_ring_brightness_flash) + "\" min=\"0\" max=\"255\"><br>";
  html += "<input type=\"submit\" value=\"Save\">";
  html += "</form></body></html>";

  server.send(200, "text/html", html);
}

// Handle the form submission and save settings
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password") &&
      server.hasArg("chaseSpeed") && server.hasArg("chaseInterval") &&
      server.hasArg("flashInterval") && server.hasArg("brightness") &&
      server.hasArg("flashBrightness")) {

    // Retrieve form data
    ssid = server.arg("ssid");
    password = server.arg("password");
    chaseSpeed = server.arg("chaseSpeed").toInt();
    chaseInterval = server.arg("chaseInterval").toInt();
    flashInterval = server.arg("flashInterval").toInt();
    led_ring_brightness = server.arg("brightness").toInt();
    led_ring_brightness_flash = server.arg("flashBrightness").toInt();

    // Save to Preferences
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putInt("chaseSpeed", chaseSpeed);
    preferences.putInt("chaseInterval", chaseInterval);
    preferences.putInt("flashInterval", flashInterval);
    preferences.putInt("brightness", led_ring_brightness);
    preferences.putInt("flashBrightness", led_ring_brightness_flash);

    // Respond to the client
    String html = "<!DOCTYPE html><html><head><title>Settings Saved</title></head><body>";
    html += "<h1>Settings Saved Successfully!</h1>";
    html += "<p>The device will now restart to apply new settings.</p>";
    html += "</body></html>";

    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart(); // Restart to apply new settings
  } else {
    server.send(400, "text/html", "<h1>Bad Request</h1><p>Missing form fields.</p>");
  }
}

// -------------------------- Time Synchronization --------------------------

// Initialize time synchronization with NTP server
void initializeTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", TIMEZONE, 1); // Set timezone
  tzset();
  Serial.println("Time synchronized with NTP server");
}

// -------------------------- LED Control Functions --------------------------

// Set all LEDs to blue background
void blue_light() {
  pixels.setBrightness(led_ring_brightness);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(COLOR_BLUE));
  }
  pixels.show();
}

// Flash all LEDs in white
void flash_cuckoo() {
  pixels.setBrightness(led_ring_brightness_flash);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(COLOR_WHITE));
  }
  pixels.show();
}

// Blink green LEDs in AP mode
void blink_green() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousBlinkMillis >= blinkInterval) {
    previousBlinkMillis = currentMillis;
    if (greenOn) {
      pixels.clear();
      pixels.show();
      greenOn = false;
    } else {
      pixels.setBrightness(led_ring_brightness_flash);
      for (int i = 0; i < NUMPIXELS; i++) {
        pixels.setPixelColor(i, pixels.Color(COLOR_GREEN));
      }
      pixels.show();
      greenOn = true;
    }
  }
}