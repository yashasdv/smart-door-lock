#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// ==========================================
// 1. WIFI AND MQTT CONFIGURATION
// ==========================================

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* BROKER = "broker.emqx.io";
const int MQTT_PORT = 1883;

const char* TOPIC = "ssse/smartlock/yashas113/cmd";
const char* CLIENT_ID = "SmartDoorLock_Yashas113";

// ==========================================
// 2. GPIO CONFIGURATION
// ==========================================

#define SERVO_PIN 18
#define RED_LED 27
#define BUZZER 25

// ==========================================
// 3. TIMING CONFIGURATION
// ==========================================

const unsigned long DOOR_OPEN_TIME = 5000;
const unsigned long ALERT_TIME = 1000;
const unsigned long COMPLAINT_INTERVAL = 2000;
const unsigned long LOCKOUT_TIME = 10000;

// ==========================================
// 4. GLOBAL OBJECTS AND VARIABLES
// ==========================================

WiFiClient espClient;
PubSubClient mqtt(espClient);
Servo doorServo;

bool doorOpen = false;
unsigned long doorOpenedAt = 0;

bool alertActive = false;
unsigned long alertStartedAt = 0;

int complaintCount = 0;
bool hasPreviousComplaint = false;
unsigned long lastComplaintAt = 0;

bool lockoutActive = false;
unsigned long lockoutStartedAt = 0;

unsigned long lastMqttAttempt = 0;
const unsigned long MQTT_RETRY_INTERVAL = 3000;

// ==========================================
// 5. WIFI CONNECTION
// ==========================================

void setup_wifi() {
  Serial.print("Connecting to WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ==========================================
// 6. AUTHORIZED VISITOR
// ==========================================

void handleAuthorized() {
  Serial.println("CLASS 1: AUTHORIZED VISITOR");

  // Reset consecutive complaints
  complaintCount = 0;
  hasPreviousComplaint = false;

  // Stop previous alert
  alertActive = false;

  digitalWrite(RED_LED, LOW);
  digitalWrite(BUZZER, LOW);

  // Open door
  doorServo.write(90);

  doorOpen = true;
  doorOpenedAt = millis();

  Serial.println("Door opened for 5 seconds.");
}

// ==========================================
// 7. UNAUTHORIZED VISITOR
// ==========================================

void handleUnauthorized() {
  unsigned long now = millis();

  Serial.println("CLASS 2: UNAUTHORIZED VISITOR");

  if (lockoutActive) {
    Serial.println("Command ignored: Security lockout active.");
    return;
  }

  // Require 2 seconds between complaints
  if (hasPreviousComplaint &&
      now - lastComplaintAt < COMPLAINT_INTERVAL) {
    Serial.println("Complaint ignored: Too soon.");
    return;
  }

  complaintCount++;
  lastComplaintAt = now;
  hasPreviousComplaint = true;

  Serial.print("Complaint count: ");
  Serial.println(complaintCount);

  // Activate alert
  digitalWrite(RED_LED, HIGH);
  digitalWrite(BUZZER, HIGH);

  alertActive = true;
  alertStartedAt = now;

  // Third complaint activates lockout
  if (complaintCount >= 3) {
    lockoutActive = true;
    lockoutStartedAt = now;

    // Keep red LED ON during lockout
    digitalWrite(RED_LED, HIGH);

    Serial.println("THIRD COMPLAINT DETECTED!");
    Serial.println("SECURITY LOCKOUT: 10 SECONDS");
  }
}

// ==========================================
// 8. MQTT MESSAGE CALLBACK
// ==========================================

void callback(char* topic, byte* payload, unsigned int length) {
  String command = "";

  for (unsigned int i = 0; i < length; i++) {
    command += (char)payload[i];
  }

  command.trim();

  Serial.println("\n-------------------------");
  Serial.print("Received command: ");
  Serial.println(command);

  Serial.print("Lockout active: ");
  Serial.println(lockoutActive ? "YES" : "NO");

  // Reject ALL commands during lockout
  if (lockoutActive) {
    unsigned long elapsed = millis() - lockoutStartedAt;

    unsigned long remaining =
        (elapsed < LOCKOUT_TIME)
        ? LOCKOUT_TIME - elapsed
        : 0;

    Serial.print("Lockout time remaining: ");
    Serial.print(remaining);
    Serial.println(" ms");

    Serial.println("LOCKOUT ACTIVE: Command ignored.");
    Serial.println("-------------------------");

    return;
  }

  if (command == "open") {
    handleAuthorized();
  } else if (command == "deny") {
    handleUnauthorized();
  } else {
    Serial.println("Unknown command.");
  }

  Serial.println("-------------------------");
}

// ==========================================
// 9. MQTT CONNECTION MANAGEMENT
// ==========================================

void reconnect_mqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Waiting for WiFi...");
    return;
  }

  unsigned long now = millis();

  // Avoid continuous MQTT reconnection attempts
  if (now - lastMqttAttempt < MQTT_RETRY_INTERVAL) {
    return;
  }

  lastMqttAttempt = now;

  Serial.print("Connecting to MQTT...");

  if (mqtt.connect(CLIENT_ID)) {
    Serial.println("Connected!");

    if (mqtt.subscribe(TOPIC)) {
      Serial.print("Subscribed to: ");
      Serial.println(TOPIC);
    } else {
      Serial.println("Subscription failed!");
    }
  } else {
    Serial.print("MQTT connection failed. State: ");
    Serial.println(mqtt.state());
  }
}

// ==========================================
// 10. NON-BLOCKING TIMER MANAGEMENT
// ==========================================

void updateTimers() {
  unsigned long now = millis();

  // Automatically close door after 5 seconds
  if (doorOpen &&
      now - doorOpenedAt >= DOOR_OPEN_TIME) {
    doorServo.write(0);
    doorOpen = false;

    Serial.println("Door automatically closed.");
  }

  // Stop buzzer after alert duration
  if (alertActive &&
      now - alertStartedAt >= ALERT_TIME) {
    digitalWrite(BUZZER, LOW);

    if (!lockoutActive) {
      digitalWrite(RED_LED, LOW);
    }

    alertActive = false;

    Serial.println("Alert finished.");
  }

  // End lockout after 10 seconds
  if (lockoutActive &&
      now - lockoutStartedAt >= LOCKOUT_TIME) {
    lockoutActive = false;

    complaintCount = 0;
    hasPreviousComplaint = false;

    digitalWrite(RED_LED, LOW);
    digitalWrite(BUZZER, LOW);

    alertActive = false;

    Serial.println("LOCKOUT ENDED.");
    Serial.println("Normal operation resumed.");
  }
}

// ==========================================
// 11. SETUP
// ==========================================

void setup() {
  Serial.begin(115200);

  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RED_LED, LOW);
  digitalWrite(BUZZER, LOW);

  doorServo.attach(SERVO_PIN);
  doorServo.write(0);

  setup_wifi();

  mqtt.setServer(BROKER, MQTT_PORT);
  mqtt.setCallback(callback);

  // Short MQTT operation timeout
  mqtt.setSocketTimeout(3);

  Serial.println("Smart Door Lock initialized.");
}

// ==========================================
// 12. MAIN LOOP
// ==========================================

void loop() {
  if (!mqtt.connected()) {
    reconnect_mqtt();
  } else {
    mqtt.loop();
  }

  updateTimers();
}
