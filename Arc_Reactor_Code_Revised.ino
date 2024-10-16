#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <time.h>
#include "Adafruit_NeoPixel.h"

// -------------------------- Configuration --------------------------

// Pin and NeoPixel configuration
#define NEOPIXEL_PIN    17          // GPIO pin connected to the NeoPixel ring
#define NUMPIXELS       35          // Number of NeoPixels in the ring

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
int numChasers = 1;             // Number of chasers in the chase effect
int fadeDuration = 2000;        // Fade duration in milliseconds

// Color Variables (Default RGB Values)
int backgroundColorR = 0;
int backgroundColorG = 20;
int backgroundColorB = 255;

int flashColorR = 250;
int flashColorG = 250;
int flashColorB = 250;

int wifiLostColorR = 255;
int wifiLostColorG = 0;
int wifiLostColorB = 0;

int apModeColorR = 0;
int apModeColorG = 255;
int apModeColorB = 0;

// LED Control Variables
bool chaseActive = false;
unsigned long previousChaseMillis = 0;
int chasePositions[10];         // Maximum of 10 chasers
int numChasersMax = 10;         // Maximum number of chasers allowed

bool flashActive = false;
unsigned long lastFlashStartMillis = 0;

// Fade Variables
bool fadeActive = false;
unsigned long fadeStartMillis = 0;

// Variables to prevent multiple triggers within the same minute
int lastFlashMinute = -1;
int lastChaseMinute = -1;

// Wi-Fi Monitoring Variables
bool wifiConnected = false;
unsigned long previousWifiCheckMillis = 0;
const unsigned long wifiCheckInterval = 5000; // milliseconds

// Wi-Fi Lost Indication Variables
bool wifiLostActive = false;
unsigned long previousWifiLostMillis = 0;
const unsigned long wifiLostBlinkInterval = 1000; // milliseconds
bool redOn = false;

// AP Mode Blinking LEDs Variables
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
void fade_to_blue();

// -------------------------- Setup Function --------------------------

void setup() {
  // Initialize Serial for debugging
  Serial.begin(115200);
  Serial.println("\nStarting Arc Reactor Configuration...");

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
  numChasers = preferences.getInt("numChasers", 1);
  fadeDuration = preferences.getInt("fadeDuration", 2000);

  // Color settings
  backgroundColorR = preferences.getInt("bgColorR", 0);
  backgroundColorG = preferences.getInt("bgColorG", 20);
  backgroundColorB = preferences.getInt("bgColorB", 255);

  flashColorR = preferences.getInt("flashColorR", 250);
  flashColorG = preferences.getInt("flashColorG", 250);
  flashColorB = preferences.getInt("flashColorB", 250);

  wifiLostColorR = preferences.getInt("wifiLostColorR", 255);
  wifiLostColorG = preferences.getInt("wifiLostColorG", 0);
  wifiLostColorB = preferences.getInt("wifiLostColorB", 0);

  apModeColorR = preferences.getInt("apModeColorR", 0);
  apModeColorG = preferences.getInt("apModeColorG", 255);
  apModeColorB = preferences.getInt("apModeColorB", 0);

  // Ensure numChasers is within allowed range
  if (numChasers < 1) numChasers = 1;
  if (numChasers > numChasersMax) numChasers = numChasersMax;

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
        blue_light(); // Restore background color
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
          pixels.setPixelColor(i, pixels.Color(wifiLostColorR, wifiLostColorG, wifiLostColorB));
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
  if (!chaseActive) {
    if ((chaseInterval == 0) ||
        (currentMinute % chaseInterval == 0 && currentSecond == 0 && currentMinute != lastChaseMinute)) {
      chaseActive = true;
      // Initialize chaser positions
      for (int i = 0; i < numChasers; i++) {
        chasePositions[i] = (i * NUMPIXELS) / numChasers;
      }
      previousChaseMillis = currentMillis;
      lastChaseMinute = currentMinute;
    }
  }

  // Update the chase effect
  if (chaseActive && (currentMillis - previousChaseMillis >= chaseSpeed)) {
    previousChaseMillis += chaseSpeed;

    // Update each chaser
    for (int i = 0; i < numChasers; i++) {
      // Reset the previous pixel to background color
      int previousPixel = (chasePositions[i] - 1 + NUMPIXELS) % NUMPIXELS;
      pixels.setPixelColor(previousPixel, pixels.Color(backgroundColorR, backgroundColorG, backgroundColorB));

      // Set the current pixel to flash color
      pixels.setPixelColor(chasePositions[i], pixels.Color(flashColorR, flashColorG, flashColorB));

      // Move to the next pixel
      chasePositions[i] = (chasePositions[i] + 1) % NUMPIXELS;
    }
    pixels.show();

    // Check if all chasers have completed a full rotation
    static int rotations = 0;
    if (chasePositions[0] == 0) {
      rotations++;
      if (chaseInterval != 0 && rotations >= 1) {
        chaseActive = false;
        rotations = 0;
        // Reset pixels to background color
        blue_light();
      }
    }
  }

  // Start the flash effect at specified intervals
  if (!flashActive && !fadeActive) {
    if ((flashInterval == 0) ||
        (currentMinute % flashInterval == 0 && currentSecond == 0 && currentMinute != lastFlashMinute)) {
      flashActive = true;
      lastFlashStartMillis = currentMillis;
      lastFlashMinute = currentMinute;
      flash_cuckoo();
    }
  }

  // Handle the flash effect duration and fading
  if (flashActive) {
    if (currentMillis - lastFlashStartMillis >= flashInterval) {
      // Flash duration is over, start fade
      flashActive = false;
      fadeActive = true;
      fadeStartMillis = currentMillis;
    }
  }

  if (fadeActive) {
    fade_to_blue();
    if (currentMillis - fadeStartMillis >= fadeDuration) {
      fadeActive = false;
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
  String html = "<!DOCTYPE html><html><head><title>Arc Reactor Configuration</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; }";
  html += "input[type='text'], input[type='password'], input[type='number'] { width: 100%; padding: 8px; margin: 4px 0; }";
  html += "input[type='submit'], input[type='button'], select { padding: 10px 20px; }";
  html += ".color-input { width: 60px; display: inline-block; }";
  html += "</style></head><body>";
  html += "<h1>Arc Reactor Configuration</h1>";
  html += "<form action=\"/save\" method=\"POST\">";
  html += "<h2>Wi-Fi Settings</h2>";
  html += "Wi-Fi SSID:<br><input type=\"text\" name=\"ssid\" value=\"" + ssid + "\"><br>";
  html += "Wi-Fi Password:<br><input type=\"password\" name=\"password\" value=\"" + password + "\"><br>";
  html += "<h2>Effect Settings</h2>";
  html += "Chase Speed (ms per step):<br><input type=\"number\" name=\"chaseSpeed\" value=\"" + String(chaseSpeed) + "\"><br>";
  html += "Chase Interval (minutes, 0=continuous):<br><input type=\"number\" name=\"chaseInterval\" value=\"" + String(chaseInterval) + "\"><br>";
  html += "Number of Chasers (1-" + String(numChasersMax) + "):<br><input type=\"number\" name=\"numChasers\" value=\"" + String(numChasers) + "\" min=\"1\" max=\"" + String(numChasersMax) + "\"><br>";
  html += "Flash Interval (minutes, 0=continuous):<br><input type=\"number\" name=\"flashInterval\" value=\"" + String(flashInterval) + "\"><br>";
  html += "Fade Duration (ms):<br><input type=\"number\" name=\"fadeDuration\" value=\"" + String(fadeDuration) + "\"><br>";
  html += "<h2>Brightness Settings</h2>";
  html += "LED Brightness (0-255):<br><input type=\"number\" name=\"brightness\" value=\"" + String(led_ring_brightness) + "\" min=\"0\" max=\"255\"><br>";
  html += "Flash Brightness (0-255):<br><input type=\"number\" name=\"flashBrightness\" value=\"" + String(led_ring_brightness_flash) + "\" min=\"0\" max=\"255\"><br>";
  html += "<h2>Color Settings</h2>";
  // Background Color
  html += "<strong>Background Color (RGB):</strong><br>";
  html += "R: <input type=\"number\" name=\"bgColorR\" value=\"" + String(backgroundColorR) + "\" min=\"0\" max=\"255\" class=\"color-input\">";
  html += " G: <input type=\"number\" name=\"bgColorG\" value=\"" + String(backgroundColorG) + "\" min=\"0\" max=\"255\" class=\"color-input\">";
  html += " B: <input type=\"number\" name=\"bgColorB\" value=\"" + String(backgroundColorB) + "\" min=\"0\" max=\"255\" class=\"color-input\"><br>";
  // Flash Color
  html += "<strong>Flash Color (RGB):</strong><br>";
  html += "R: <input type=\"number\" name=\"flashColorR\" value=\"" + String(flashColorR) + "\" min=\"0\" max=\"255\" class=\"color-input\">";
  html += " G: <input type=\"number\" name=\"flashColorG\" value=\"" + String(flashColorG) + "\" min=\"0\" max=\"255\" class=\"color-input\">";
  html += " B: <input type=\"number\" name=\"flashColorB\" value=\"" + String(flashColorB) + "\" min=\"0\" max=\"255\" class=\"color-input\"><br>";
  // Wi-Fi Lost Color
  html += "<strong>Wi-Fi Lost Color (RGB):</strong><br>";
  html += "R: <input type=\"number\" name=\"wifiLostColorR\" value=\"" + String(wifiLostColorR) + "\" min=\"0\" max=\"255\" class=\"color-input\">";
  html += " G: <input type=\"number\" name=\"wifiLostColorG\" value=\"" + String(wifiLostColorG) + "\" min=\"0\" max=\"255\" class=\"color-input\">";
  html += " B: <input type=\"number\" name=\"wifiLostColorB\" value=\"" + String(wifiLostColorB) + "\" min=\"0\" max=\"255\" class=\"color-input\"><br>";
  // AP Mode Color
  html += "<strong>AP Mode Color (RGB):</strong><br>";
  html += "R: <input type=\"number\" name=\"apModeColorR\" value=\"" + String(apModeColorR) + "\" min=\"0\" max=\"255\" class=\"color-input\">";
  html += " G: <input type=\"number\" name=\"apModeColorG\" value=\"" + String(apModeColorG) + "\" min=\"0\" max=\"255\" class=\"color-input\">";
  html += " B: <input type=\"number\" name=\"apModeColorB\" value=\"" + String(apModeColorB) + "\" min=\"0\" max=\"255\" class=\"color-input\"><br>";
  html += "<br><input type=\"submit\" value=\"Save\">";
  html += "</form></body></html>";

  server.send(200, "text/html", html);
}

// Handle the form submission and save settings
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password") &&
      server.hasArg("chaseSpeed") && server.hasArg("chaseInterval") &&
      server.hasArg("flashInterval") && server.hasArg("brightness") &&
      server.hasArg("flashBrightness") && server.hasArg("numChasers") &&
      server.hasArg("fadeDuration") &&
      server.hasArg("bgColorR") && server.hasArg("bgColorG") && server.hasArg("bgColorB") &&
      server.hasArg("flashColorR") && server.hasArg("flashColorG") && server.hasArg("flashColorB") &&
      server.hasArg("wifiLostColorR") && server.hasArg("wifiLostColorG") && server.hasArg("wifiLostColorB") &&
      server.hasArg("apModeColorR") && server.hasArg("apModeColorG") && server.hasArg("apModeColorB")) {

    // Retrieve form data
    ssid = server.arg("ssid");
    password = server.arg("password");
    chaseSpeed = server.arg("chaseSpeed").toInt();
    chaseInterval = server.arg("chaseInterval").toInt();
    flashInterval = server.arg("flashInterval").toInt();
    led_ring_brightness = server.arg("brightness").toInt();
    led_ring_brightness_flash = server.arg("flashBrightness").toInt();
    numChasers = server.arg("numChasers").toInt();
    fadeDuration = server.arg("fadeDuration").toInt();

    // Color settings
    backgroundColorR = server.arg("bgColorR").toInt();
    backgroundColorG = server.arg("bgColorG").toInt();
    backgroundColorB = server.arg("bgColorB").toInt();

    flashColorR = server.arg("flashColorR").toInt();
    flashColorG = server.arg("flashColorG").toInt();
    flashColorB = server.arg("flashColorB").toInt();

    wifiLostColorR = server.arg("wifiLostColorR").toInt();
    wifiLostColorG = server.arg("wifiLostColorG").toInt();
    wifiLostColorB = server.arg("wifiLostColorB").toInt();

    apModeColorR = server.arg("apModeColorR").toInt();
    apModeColorG = server.arg("apModeColorG").toInt();
    apModeColorB = server.arg("apModeColorB").toInt();

    // Ensure numChasers is within allowed range
    if (numChasers < 1) numChasers = 1;
    if (numChasers > numChasersMax) numChasers = numChasersMax;

    // Save to Preferences
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putInt("chaseSpeed", chaseSpeed);
    preferences.putInt("chaseInterval", chaseInterval);
    preferences.putInt("flashInterval", flashInterval);
    preferences.putInt("brightness", led_ring_brightness);
    preferences.putInt("flashBrightness", led_ring_brightness_flash);
    preferences.putInt("numChasers", numChasers);
    preferences.putInt("fadeDuration", fadeDuration);

    // Save color settings
    preferences.putInt("bgColorR", backgroundColorR);
    preferences.putInt("bgColorG", backgroundColorG);
    preferences.putInt("bgColorB", backgroundColorB);

    preferences.putInt("flashColorR", flashColorR);
    preferences.putInt("flashColorG", flashColorG);
    preferences.putInt("flashColorB", flashColorB);

    preferences.putInt("wifiLostColorR", wifiLostColorR);
    preferences.putInt("wifiLostColorG", wifiLostColorG);
    preferences.putInt("wifiLostColorB", wifiLostColorB);

    preferences.putInt("apModeColorR", apModeColorR);
    preferences.putInt("apModeColorG", apModeColorG);
    preferences.putInt("apModeColorB", apModeColorB);

    // Redirect back to the root page after saving
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");

    // Delay to ensure response is sent before restarting
    delay(100);
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

// Set all LEDs to background color
void blue_light() {
  pixels.setBrightness(led_ring_brightness);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(backgroundColorR, backgroundColorG, backgroundColorB));
  }
  pixels.show();
}

// Flash all LEDs in flash color
void flash_cuckoo() {
  pixels.setBrightness(led_ring_brightness_flash);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(flashColorR, flashColorG, flashColorB));
  }
  pixels.show();
}

// Blink LEDs in AP mode color
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
        pixels.setPixelColor(i, pixels.Color(apModeColorR, apModeColorG, apModeColorB));
      }
      pixels.show();
      greenOn = true;
    }
  }
}

// Fade from flash color to background color
void fade_to_blue() {
  unsigned long currentMillis = millis();
  float progress = (float)(currentMillis - fadeStartMillis) / fadeDuration;
  if (progress > 1.0) progress = 1.0;

  uint8_t r_start = flashColorR; // Starting from flash color
  uint8_t g_start = flashColorG;
  uint8_t b_start = flashColorB;

  uint8_t r_end = backgroundColorR;   // Ending at background color
  uint8_t g_end = backgroundColorG;
  uint8_t b_end = backgroundColorB;

  uint8_t r = r_start + progress * (r_end - r_start);
  uint8_t g = g_start + progress * (g_end - g_start);
  uint8_t b = b_start + progress * (b_end - b_start);

  pixels.setBrightness(led_ring_brightness);
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}