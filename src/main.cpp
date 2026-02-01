/*
 * Smart Sheet - ESP32 Bluetooth Motor Controller
 * Controls 8 motors via PWM with pattern support
 * Communication: Bluetooth Classic SPP
 * 
 * Motor Pins: D18, D19, D21, D22, D23, D25, D26, D27
 */

#include <Arduino.h>
#include "BluetoothSerial.h"

// Check if Bluetooth is enabled
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` and enable it
#endif

BluetoothSerial SerialBT;

// ==================== PIN CONFIGURATION ====================
const int MOTOR_PINS[8] = {18, 19, 21, 22, 23, 25, 26, 27};
const int NUM_MOTORS = 8;

// ==================== PWM CONFIGURATION ====================
const int PWM_FREQUENCY = 5000;    // 5 KHz
const int PWM_RESOLUTION = 8;      // 8-bit resolution (0-255)

// ==================== PATTERN MODES ====================
enum PatternMode {
  MODE_STOP,
  MODE_CONSTANT,
  MODE_WAVE
};

// ==================== GLOBAL VARIABLES ====================
PatternMode currentMode = MODE_STOP;
int globalIntensity = 128;         // Default 50% intensity
int waveSpeed = 100;               // Wave delay in milliseconds
int currentWavePosition = 0;
unsigned long lastWaveUpdate = 0;

// Motor intensity array for individual control
int motorIntensities[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// ==================== FUNCTION DECLARATIONS ====================
void handleBluetoothInput();
void handleSerialInput();
void processCommand(String command);
void setMode(String mode);
void setIntensity(int value);
void setWaveSpeed(int value);
void sendStatus();
void stopAllMotors();
void executePattern();
void executeConstantPattern();
void executeWavePattern();

// ==================== SETUP ====================
void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  Serial.println("================================");
  Serial.println("Smart Sheet Motor Controller");
  Serial.println("ESP32 Starting...");
  Serial.println("================================");
  
  // Initialize Bluetooth
  if (!SerialBT.begin("SmartSheet_ESP32")) {
    Serial.println("ERROR: Bluetooth initialization failed!");
    while (1); // Halt if Bluetooth fails
  }
  Serial.println("Bluetooth initialized: SmartSheet_ESP32");
  Serial.println("Waiting for connection...");
  
  // Initialize PWM channels for each motor
  for (int i = 0; i < NUM_MOTORS; i++) {
    ledcSetup(i, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(MOTOR_PINS[i], i);
    ledcWrite(i, 0); // Start with motors off
    Serial.printf("Motor %d initialized on GPIO %d (PWM Channel %d)\n", 
                  i + 1, MOTOR_PINS[i], i);
  }
  
  Serial.println("================================");
  Serial.println("System Ready!");
  Serial.println("Commands: MODE:STOP, MODE:CONSTANT, MODE:WAVE");
  Serial.println("          INTENSITY:0-255, SPEED:50-500, STATUS");
  Serial.println("================================\n");
}

// ==================== MAIN LOOP ====================
void loop() {
  // Handle Bluetooth commands
  handleBluetoothInput();
  
  // Handle Serial Monitor commands (for debugging)
  handleSerialInput();
  
  // Execute current pattern
  executePattern();
}

// ==================== BLUETOOTH INPUT HANDLER ====================
void handleBluetoothInput() {
  if (SerialBT.available()) {
    String command = SerialBT.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      Serial.print("BT Received: ");
      Serial.println(command);
      processCommand(command);
    }
  }
}

// ==================== SERIAL INPUT HANDLER ====================
void handleSerialInput() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      Serial.print("Serial Received: ");
      Serial.println(command);
      processCommand(command);
    }
  }
}

// ==================== COMMAND PROCESSOR ====================
void processCommand(String command) {
  command.toUpperCase();
  
  if (command.startsWith("MODE:")) {
    String mode = command.substring(5);
    setMode(mode);
  }
  else if (command.startsWith("INTENSITY:")) {
    int value = command.substring(10).toInt();
    setIntensity(value);
  }
  else if (command.startsWith("SPEED:")) {
    int value = command.substring(6).toInt();
    setWaveSpeed(value);
  }
  else if (command == "STATUS") {
    sendStatus();
  }
  else {
    String errorMsg = "ERROR: Unknown command - " + command;
    Serial.println(errorMsg);
    SerialBT.println(errorMsg);
  }
}

// ==================== MODE SETTER ====================
void setMode(String mode) {
  String response;
  
  if (mode == "STOP") {
    currentMode = MODE_STOP;
    stopAllMotors();
    response = "OK:MODE:STOP";
  }
  else if (mode == "CONSTANT") {
    currentMode = MODE_CONSTANT;
    response = "OK:MODE:CONSTANT";
  }
  else if (mode == "WAVE") {
    currentMode = MODE_WAVE;
    currentWavePosition = 0;
    response = "OK:MODE:WAVE";
  }
  else {
    response = "ERROR:INVALID_MODE";
  }
  
  Serial.println(response);
  SerialBT.println(response);
}

// ==================== INTENSITY SETTER ====================
void setIntensity(int value) {
  String response;
  
  if (value >= 0 && value <= 255) {
    globalIntensity = value;
    response = "OK:INTENSITY:" + String(value);
  }
  else {
    response = "ERROR:INTENSITY_OUT_OF_RANGE";
  }
  
  Serial.println(response);
  SerialBT.println(response);
}

// ==================== WAVE SPEED SETTER ====================
void setWaveSpeed(int value) {
  String response;
  
  if (value >= 50 && value <= 500) {
    waveSpeed = value;
    response = "OK:SPEED:" + String(value);
  }
  else {
    response = "ERROR:SPEED_OUT_OF_RANGE";
  }
  
  Serial.println(response);
  SerialBT.println(response);
}

// ==================== STATUS SENDER ====================
void sendStatus() {
  String modeStr;
  switch (currentMode) {
    case MODE_STOP:     modeStr = "STOP"; break;
    case MODE_CONSTANT: modeStr = "CONSTANT"; break;
    case MODE_WAVE:     modeStr = "WAVE"; break;
  }
  
  String status = "STATUS:MODE:" + modeStr + 
                  ",INTENSITY:" + String(globalIntensity) + 
                  ",SPEED:" + String(waveSpeed);
  
  Serial.println(status);
  SerialBT.println(status);
}

// ==================== STOP ALL MOTORS ====================
void stopAllMotors() {
  for (int i = 0; i < NUM_MOTORS; i++) {
    motorIntensities[i] = 0;
    ledcWrite(i, 0);
  }
  Serial.println("All motors stopped");
}

// ==================== PATTERN EXECUTOR ====================
void executePattern() {
  switch (currentMode) {
    case MODE_STOP:
      // Motors already stopped, do nothing
      break;
      
    case MODE_CONSTANT:
      executeConstantPattern();
      break;
      
    case MODE_WAVE:
      executeWavePattern();
      break;
  }
}

// ==================== CONSTANT PATTERN ====================
void executeConstantPattern() {
  for (int i = 0; i < NUM_MOTORS; i++) {
    if (motorIntensities[i] != globalIntensity) {
      motorIntensities[i] = globalIntensity;
      ledcWrite(i, globalIntensity);
    }
  }
}

// ==================== WAVE PATTERN ====================
void executeWavePattern() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastWaveUpdate >= (unsigned long)waveSpeed) {
    lastWaveUpdate = currentTime;
    
    // Calculate intensity for each motor based on wave position
    for (int i = 0; i < NUM_MOTORS; i++) {
      // Create a sine wave effect
      float phase = (float)(i - currentWavePosition) / NUM_MOTORS * 2 * PI;
      float waveValue = (sin(phase) + 1) / 2; // Normalize to 0-1
      int intensity = (int)(waveValue * globalIntensity);
      
      motorIntensities[i] = intensity;
      ledcWrite(i, intensity);
    }
    
    // Move wave position
    currentWavePosition = (currentWavePosition + 1) % NUM_MOTORS;
    
    // Debug output
    Serial.print("Wave Position: ");
    Serial.print(currentWavePosition);
    Serial.print(" | Intensities: ");
    for (int i = 0; i < NUM_MOTORS; i++) {
      Serial.print(motorIntensities[i]);
      Serial.print(" ");
    }
    Serial.println();
  }
}
