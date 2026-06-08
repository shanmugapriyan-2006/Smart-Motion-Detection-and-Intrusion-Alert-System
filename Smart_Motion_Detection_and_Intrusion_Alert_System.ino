#include <WiFi.h>
#include <PubSubClient.h>

// ==================== CONFIGURATION ====================
// 1. Wi-Fi Settings
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// 2. MQTT Settings (Using HiveMQ public broker as a standard default)
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port     = 1883;

// 3. MQTT Topics
const char* topic_status = "home/security/status"; // Topic to RECEIVE "I am outside" / "I am home"
const char* topic_alerts = "home/security/alerts"; // Topic to SEND alerts

// 4. Hardware Pin Assignments
const int PIR_PIN   = 13; // PIR Sensor Output Pin
const int LDR_PIN   = 34; // LDR Sensor Analog Pin (ADC1)
const int LIGHT_PIN = 14; // Relay/LED Pin for Light Control

// 5. Thresholds
const int DARK_THRESHOLD = 1500; // Adjust based on your room's darkness (0-4095)

// ==================== GLOBAL VARIABLES ====================
bool isOwnerAway = false; // Tracks state: false = "I am home", true = "I am outside"
unsigned long lastAlertTime = 0;
const unsigned long alertCooldown = 15000; // 15-second delay to prevent notification spamming

WiFiClient espClient;
PubSubClient client(espClient);

// ==================== FUNCTIONS ====================

// SETUP WI-FI
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("Wi-Fi connected successfully!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// MQTT CALLBACK (Receives incoming messages from your phone)
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Message arrived on topic [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // Check if message came from the correct status topic
  if (String(topic) == topic_status) {
    message.trim(); // Remove any accidental spaces or hidden newline characters
    
    if (message == "I am outside" || message == "OUT") {
      isOwnerAway = true;
      Serial.println(">>> System State updated to: SECURITY MODE (Armed)");
    } 
    else if (message == "I am home" || message == "HOME") {
      isOwnerAway = false;
      Serial.println(">>> System State updated to: CONVENIENCE MODE (Disarmed)");
      digitalWrite(LIGHT_PIN, LOW); // Turn off light immediately if left on
    }
  }
}

// MQTT RECONNECT (Ensures connection is always alive)
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Create a unique client ID using ESP32 MAC address
    String clientId = "ESP32Client-" + String(random(0, 0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected!");
      
      // Once connected, subscribe to receive status messages
      client.subscribe(topic_status);
      Serial.print("Subscribed to topic: ");
      Serial.println(topic_status);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 5 seconds");
      delay(5000);
    }
  }
}

// ==================== MAIN SETUP & LOOP ====================

void setup() {
  Serial.begin(115200);
  
  // Pin modes
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW); // Initialize light to OFF

  setup_wifi();
  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  // Always verify network and broker connections are active
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Keeps the MQTT client processing incoming/outgoing data

  // Sensor Readings
  int ldrValue = analogRead(LDR_PIN);
  int pirValue = digitalRead(PIR_PIN);

  // Core Security & Lighting Logic
  if (ldrValue > DARK_THRESHOLD) { // High ADC value usually means dark for standard LDR pull-down circuits
    if (pirValue == HIGH) {
      
      if (!isOwnerAway) {
        // OWNER IS HOME: Turn on convenience light
        digitalWrite(LIGHT_PIN, HIGH);
        Serial.println("Motion detected (Owner Home) -> Light turned ON.");
      } 
      else {
        // OWNER IS OUTSIDE: Do not trigger light, send critical alert immediately
        digitalWrite(LIGHT_PIN, LOW); 
        
        // Cooldown mechanism so your phone isn't spammed with 100 texts a minute
        if (millis() - lastAlertTime > alertCooldown) {
          Serial.println("Motion detected (Owner Outside!) -> Sending MQTT Alert.");
          client.publish(topic_alerts, "ALERT: Intruder detected! Motion identified while you are marked as OUTSIDE.");
          lastAlertTime = millis();
        }
      }
    } else {
      // If no motion is detected and owner is home, turn off the light after a while
      // (For prototyping simplicity, it turns off immediately when PIR goes LOW)
      if (!isOwnerAway) {
        digitalWrite(LIGHT_PIN, LOW);
      }
    }
  } else {
    // It is daytime: Keep the light off regardless of motion
    digitalWrite(LIGHT_PIN, LOW);
  }
  
  delay(100); // Small stability delay
}