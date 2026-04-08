/*
 * BRAILLE HOME AUTOMATION - REMOTE MODULE (UPDATED)
 * - Emergency Logic:
 *   - Long press BOTH sensors = Send "ACK" (Confirm)
 *   - Long press ONE sensor   = Send "STOP" (False Alarm)
 * - Feedback System:
 *   - 'S' = Short beep/vib (400ms)
 *   - 'E' = Long beep/vib (800ms)
 *   - 'C' = 5 short beeps/vibs (correct password)
 *   - 'W' = 5 long beeps/vibs (wrong password)
 *   - 'F' = Continuous short beeps/vibs until ACK/STOP
 */

#include <WiFi.h>

const char* ssid = "ESP32-Server";
const char* password = "12345678";
const char* serverIP = "192.168.4.1";

WiFiClient tcpClient;

// Pins - SINGLE BUZZER AND VIBRATION MOTOR
const int vibMotor = 12;  // Single vibration motor
const int buzzer = 25;    // Single buzzer
const int btnPins[] = {5, 17, 16, 4, 2, 15};  // REVERSED ORDER
const int touchLeft = 32;  
const int touchRight = 33;

// Globals
String localBuffer = "";
bool emergencyMode = false;
unsigned long emergencyTimer = 0;
unsigned long lastDebounce = 0;

const byte brailleMap[] = {
  1, 3, 9, 25, 17, 11, 27, 19, 10, 26,  // A-J
  5, 7, 13, 29, 21, 15, 31, 23, 14, 30, // K-T
  37, 39, 58, 45, 61, 53                // U-Z
};

const char brailleChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

void setup() {
  Serial.begin(115200);
  
  // Init Output
  pinMode(vibMotor, OUTPUT);
  digitalWrite(vibMotor, LOW);
  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, LOW);
 
  // Init Input - REVERSED BUTTON ORDER
  for(int i=0; i<6; i++) pinMode(btnPins[i], INPUT_PULLUP);
  pinMode(touchLeft, INPUT); 
  pinMode(touchRight, INPUT);

  WiFi.mode(WIFI_STA);
  Serial.print("WiFi Init...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500);
    Serial.print("."); 
  }
  Serial.println("\nWiFi OK");
}

// Feedback Function - Updated for single motor/buzzer
void triggerFeedback(char type) {
  if (type == 'S') {
    // Short beep/vib (400ms) - Successful command
    digitalWrite(vibMotor, HIGH);
    digitalWrite(buzzer, HIGH);
    delay(400);
    digitalWrite(vibMotor, LOW);
    digitalWrite(buzzer, LOW);
  }
  else if (type == 'E') {
    // Long beep/vib (800ms) - Error/Wrong command
    digitalWrite(vibMotor, HIGH);
    digitalWrite(buzzer, HIGH);
    delay(800);
    digitalWrite(vibMotor, LOW);
    digitalWrite(buzzer, LOW);
  }
  else if (type == 'C') {
    // 5 short beeps/vibs - Correct Password
    for (int i = 0; i < 5; i++) {
      digitalWrite(vibMotor, HIGH);
      digitalWrite(buzzer, HIGH);
      delay(400);
      digitalWrite(vibMotor, LOW);
      digitalWrite(buzzer, LOW);
      if (i < 4) delay(200);
      // Gap between beeps
    }
  } // <--- THIS CLOSING BRACE WAS MISSING
  else if (type == 'W') {
    // 5 long beeps/vibs - Wrong Password
    for (int i = 0; i < 5; i++) {
      digitalWrite(vibMotor, HIGH);
      digitalWrite(buzzer, HIGH);
      delay(800);
      digitalWrite(vibMotor, LOW);
      digitalWrite(buzzer, LOW);
      if (i < 4) delay(200);
      // Gap between beeps
    }
  }
}

void maintainConnection() {
  if (WiFi.status() != WL_CONNECTED) {
     WiFi.disconnect(); 
     WiFi.begin(ssid, password);
     delay(500); 
     return;
  }
 
  if (!tcpClient.connected()) {
    if (tcpClient.connect(serverIP, 7890)) {
      Serial.println("Reconnected to Server"); 
    }
  }
}

void loop() {
  maintainConnection();

  // ---- 1. CHECK FOR ALERTS ----
  if (tcpClient.connected() && tcpClient.available()) {
    char c = tcpClient.read();
    
    if (c == 'F') {
       // Fire alert - start emergency mode
       emergencyMode = true;
       emergencyTimer = millis();
    }
    else if (c == 'S' || c == 'E' || c == 'C' || c == 'W') {
       // Feedback from appliance module
       triggerFeedback(c);
    }
  }

  // ---- 2. EMERGENCY MODE ----
  if (emergencyMode) {
     // Continuous short beep/vib pattern
     if (millis() - emergencyTimer > 1000) {
        digitalWrite(vibMotor, HIGH);
        digitalWrite(buzzer, HIGH);
        delay(400);
        digitalWrite(vibMotor, LOW);
        digitalWrite(buzzer, LOW);
        delay(600);
        emergencyTimer = millis();
     }
     
     bool tL = digitalRead(touchLeft);
     bool tR = digitalRead(touchRight);

     // Check for User Input (Any Touch detected)
     if (tL || tR) {
         // Turn off alert during user response
         digitalWrite(vibMotor, LOW);
         digitalWrite(buzzer, LOW);
         
         // Wait 2 seconds (Blocking) to confirm long press and count fingers
         delay(2000);
         
         bool finalL = digitalRead(touchLeft);
         bool finalR = digitalRead(touchRight);

         if (finalL && finalR) {
             // BOTH HELD -> Real Emergency -> Send ACK
             if (tcpClient.connected()) tcpClient.print("ACK\n");
             emergencyMode = false; 
             return;
         }
         else if (finalL || finalR) {
             // ONLY ONE HELD -> False Alarm -> Send STOP
             if (tcpClient.connected()) tcpClient.print("STOP\n");
             emergencyMode = false; 
             return;
         }
         // If neither are held after 2s, continue emergency mode
         emergencyTimer = millis(); // Reset timer
     }
     return;
  }

  // ---- 3. INPUT LOGIC ----
  bool tL = digitalRead(touchLeft);
  bool tR = digitalRead(touchRight);
 
  // A. EXECUTE COMMAND - Check for LONG PRESS on BOTH sensors
  if (tL && tR) {
    // Record start time
    unsigned long bothTouchStart = millis();
    
    // Wait to see if it's a long press (<1 second for command execution)
    delay(500);
    
    // Check if BOTH still held after 1 second
    bool finalL = digitalRead(touchLeft);
    bool finalR = digitalRead(touchRight);
    
    if (finalL && finalR) {
      // BOTH held for 1+ seconds -> EXECUTE COMMAND
      if (localBuffer.length() > 0) {
        if (tcpClient.connected()) {
           Serial.print("Sending: "); 
           Serial.println(localBuffer);
           
           tcpClient.print(localBuffer + "\n");
           localBuffer = "";
           
           // Wait for feedback from server (handled in section 1)
           delay(1000);
        } else { 
          triggerFeedback('E'); // Connection error
        }
      } else { 
        Serial.println("Buffer Empty");
        triggerFeedback('E'); // Error - no command to send
      }
      
      // Wait for release
      while(digitalRead(touchLeft) == HIGH || digitalRead(touchRight) == HIGH) {
        delay(10);
      }
      delay(500); // Debounce
    }
    // If released early, it was accidental - ignore
    return;
  }

  // B. TYPING LOGIC
  if ((tL || tR) && (millis() - lastDebounce > 400)) {
    byte pattern = 0;
    for (int i=0; i<6; i++) {
      if (digitalRead(btnPins[i]) == LOW) pattern |= (1 << i);
    }
   
    if (pattern > 0) {
      char detectedChar = '?';
      for (int i=0; i<26; i++) {
        if (brailleMap[i] == pattern) {
          if (tR) detectedChar = brailleChars[i];
          else if (tL) detectedChar = (i < 9) ? (i + 1) + '0' : (i == 9 ? '0' : '?');
          break;
        }
      }
     
      if (detectedChar != '?') {
        localBuffer += detectedChar;
        Serial.print("Buf: "); 
        Serial.println(localBuffer);
        
        // Short feedback for character entry
        digitalWrite(vibMotor, HIGH);
        digitalWrite(buzzer, HIGH);
        delay(100);
        digitalWrite(vibMotor, LOW);
        digitalWrite(buzzer, LOW);
        
        lastDebounce = millis();
      }
    }
  }
}