#include <M5Dial.h>
#include <AccelStepper.h>
#include <EEPROM.h>
#include <math.h>

// Pin definitions - SAFE PINS FOR M5DIAL
#define STEP_PIN 5
#define DIR_PIN 6
#define EN_PIN 7

// Motor and unit constants
#define STEPS_PER_REV 200
#define MICROSTEPS 16
#define GEAR_RATIO 27.0
#define LEAD_PITCH 2.0
#define STEPS_PER_UNIT (STEPS_PER_REV * MICROSTEPS * GEAR_RATIO)

// EEPROM settings
#define EEPROM_SIZE 512
#define MAX_PARTS 10
#define PART_NAME_LEN 16
#define HOME_ADDR 0
#define PARTS_ADDR 4

// UI Colors - inspired by M5Dial factory demo
#define COLOR_MANUAL    0xFD5C4C  // Red
#define COLOR_PARTS     0x577EFF  // Blue
#define COLOR_RETRACT   0xEB8429  // Orange
#define COLOR_SETUP     0x03A964  // Green
#define COLOR_BG        TFT_BLACK
#define COLOR_TEXT      0xF3E9D2  // Off-white

struct Part {
  char name[PART_NAME_LEN];
  float units;
};

Part parts[MAX_PARTS];
int partCount = 0;
long homeOffset = 0;

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

enum Mode { MANUAL_ADVANCE, KNOWN_PARTS, FULL_RETRACT, SETUP };
Mode currentMode = MANUAL_ADVANCE;
float manualUnits = 0.0;
int selectedPart = 0;
bool isDispensing = false;
int setupOption = 0;

// Encoder state
long lastEncoderPos = 0;
unsigned long lastEncoderMove = 0;

unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;
bool actionExecuted = false;

void setup() {
  Serial.begin(115200);
  Serial.println("=== M5Dial Dispenser Starting ===");

  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);  // Init with encoder, no RFID

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(HOME_ADDR, homeOffset);
  loadParts();

  Serial.println("Setting up GPIO pins (5, 6, 7)...");
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);
  Serial.println("GPIO pins OK");

  Serial.println("Setting up stepper motor...");
  stepper.setMaxSpeed(10000);
  stepper.setAcceleration(5000);
  stepper.setCurrentPosition(homeOffset);
  Serial.println("Stepper motor OK");

  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.setFont(&fonts::Orbitron_Light_24);

  lastEncoderPos = M5Dial.Encoder.read();

  drawInterface();

  Serial.println("Setup complete - ready to use");
}

void loop() {
  M5Dial.update();

  // Handle encoder rotation
  handleEncoder();

  // Handle button presses
  handleButtons();

  if (isDispensing) {
    stepper.run();

    static unsigned long lastProgressUpdate = 0;
    if (millis() - lastProgressUpdate > 50) {
      updateDisplay();
      lastProgressUpdate = millis();
    }

    if (stepper.distanceToGo() == 0) {
      isDispensing = false;
      M5Dial.Speaker.tone(8000, 100);  // Success beep
      Serial.println("Dispensing complete");
      drawInterface();
    }

    delay(5);
  } else {
    delay(50);
  }
}

void handleEncoder() {
  long newPosition = M5Dial.Encoder.read();

  if (newPosition != lastEncoderPos) {
    int delta = newPosition - lastEncoderPos;
    lastEncoderPos = newPosition;
    lastEncoderMove = millis();

    // Haptic feedback
    M5Dial.Speaker.tone(8000, 20);

    // Variable increment based on scroll speed
    unsigned long timeSinceLastMove = millis() - lastEncoderMove;
    float increment = 0.1;
    if (timeSinceLastMove < 100) {
      increment = 0.5;  // Faster scrolling = bigger steps
    }

    switch (currentMode) {
      case MANUAL_ADVANCE:
        if (!isDispensing) {
          manualUnits += (delta > 0 ? increment : -increment);
          if (manualUnits < 0) manualUnits = 0;
          if (manualUnits > 99.9) manualUnits = 99.9;
          drawInterface();
        }
        break;

      case KNOWN_PARTS:
        if (partCount > 0 && !isDispensing) {
          selectedPart += delta;
          if (selectedPart < 0) selectedPart = partCount - 1;
          if (selectedPart >= partCount) selectedPart = 0;
          drawInterface();
        }
        break;

      case SETUP:
        setupOption = (setupOption + 1) % 2;
        drawInterface();
        break;
    }
  }
}

void handleButtons() {
  bool currentlyPressed = M5Dial.BtnA.isPressed();
  
  if (currentlyPressed && !buttonWasPressed) {
    buttonPressStart = millis();
    actionExecuted = false;
    Serial.println("Button press detected");
  }
  
  if (currentlyPressed && !actionExecuted) {
    if (millis() - buttonPressStart > 800) {
      currentMode = (Mode)((currentMode + 1) % 4);
      Serial.print("LONG PRESS - Mode: ");
      Serial.println(currentMode);
      drawInterface();
      actionExecuted = true;
    }
  }
  
  if (!currentlyPressed && buttonWasPressed) {
    unsigned long pressDuration = millis() - buttonPressStart;
    Serial.print("Released after ");
    Serial.print(pressDuration);
    Serial.println("ms");
    
    if (!actionExecuted) {
      Serial.println("SHORT PRESS!");
      handleShortPress();
    }
  }
  
  buttonWasPressed = currentlyPressed;
}

void handleShortPress() {
  Serial.print("handleShortPress() called in mode: ");
  Serial.println(currentMode);
  M5Dial.Speaker.tone(8000, 50);  // Click sound

  switch (currentMode) {
    case MANUAL_ADVANCE:
      if (!isDispensing && manualUnits > 0) {
        Serial.print("Dispensing ");
        Serial.print(manualUnits);
        Serial.println(" units");
        isDispensing = true;
        stepper.moveTo(stepper.currentPosition() + (manualUnits * STEPS_PER_UNIT));
        manualUnits = 0.0;
        drawInterface();
      } else if (manualUnits == 0) {
        Serial.println("No amount set - use encoder to adjust");
      } else {
        Serial.println("Already dispensing, ignoring press");
      }
      break;

    case KNOWN_PARTS:
      if (partCount == 0) {
        Serial.println("No parts saved");
        break;
      }
      if (!isDispensing) {
        Serial.print("Dispensing part: ");
        Serial.println(parts[selectedPart].name);
        dispensePart();
      }
      break;

    case FULL_RETRACT:
      if (!isDispensing) {
        Serial.println("Full retract");
        retractFull();
      }
      break;

    case SETUP:
      if (setupOption == 0) {
        setHome();
      } else {
        saveCurrentAsNewPart();
      }
      drawInterface();
      break;
  }
}

void drawInterface() {
  M5Dial.Display.fillScreen(COLOR_BG);

  // Get current mode color
  uint32_t modeColor;
  const char* modeLabel;
  switch (currentMode) {
    case MANUAL_ADVANCE:
      modeColor = COLOR_MANUAL;
      modeLabel = "MANUAL";
      break;
    case KNOWN_PARTS:
      modeColor = COLOR_PARTS;
      modeLabel = "PARTS";
      break;
    case FULL_RETRACT:
      modeColor = COLOR_RETRACT;
      modeLabel = "RETRACT";
      break;
    case SETUP:
      modeColor = COLOR_SETUP;
      modeLabel = "SETUP";
      break;
  }

  // Draw mode indicators around the edge (small circles)
  int indicatorRadius = 100;
  for (int i = 0; i < 4; i++) {
    uint32_t color;
    switch (i) {
      case 0: color = COLOR_MANUAL; break;
      case 1: color = COLOR_PARTS; break;
      case 2: color = COLOR_RETRACT; break;
      case 3: color = COLOR_SETUP; break;
    }

    float angle = i * 90.0 - 45.0;  // Start at top-left
    float rad = angle * PI / 180.0;
    int x = 120 + indicatorRadius * cos(rad);
    int y = 120 + indicatorRadius * sin(rad);

    if (currentMode == i) {
      M5Dial.Display.fillSmoothCircle(x, y, 8, color);
    } else {
      M5Dial.Display.fillSmoothCircle(x, y, 4, color);
    }
  }

  // Draw mode label at top
  M5Dial.Display.setTextColor(COLOR_TEXT);
  M5Dial.Display.setFont(&fonts::Orbitron_Light_24);
  M5Dial.Display.setTextSize(0.8);
  M5Dial.Display.drawString(modeLabel, 120, 30);

  // Mode-specific content
  switch (currentMode) {
    case MANUAL_ADVANCE:
      M5Dial.Display.setFont(&fonts::Orbitron_Light_32);
      M5Dial.Display.setTextSize(2);
      M5Dial.Display.setTextColor(modeColor);
      M5Dial.Display.drawString(String(manualUnits, 1), 120, 110);

      M5Dial.Display.setFont(&fonts::Orbitron_Light_24);
      M5Dial.Display.setTextSize(0.6);
      M5Dial.Display.setTextColor(COLOR_TEXT);
      M5Dial.Display.drawString("mm", 120, 150);

      M5Dial.Display.setTextSize(0.5);
      if (manualUnits == 0) {
        M5Dial.Display.drawString("Turn to adjust", 120, 190);
      } else {
        M5Dial.Display.drawString("Press to GO", 120, 190);
      }
      break;

    case KNOWN_PARTS:
      if (partCount == 0) {
        M5Dial.Display.setFont(&fonts::Orbitron_Light_24);
        M5Dial.Display.setTextColor(TFT_DARKGREY);
        M5Dial.Display.setTextSize(0.7);
        M5Dial.Display.drawString("No parts", 120, 110);
        M5Dial.Display.setTextSize(0.5);
        M5Dial.Display.drawString("Use SETUP to save", 120, 140);
      } else {
        M5Dial.Display.setFont(&fonts::Orbitron_Light_24);
        M5Dial.Display.setTextSize(0.6);
        M5Dial.Display.setTextColor(COLOR_TEXT);
        M5Dial.Display.drawString(parts[selectedPart].name, 120, 80);

        M5Dial.Display.setFont(&fonts::Orbitron_Light_32);
        M5Dial.Display.setTextSize(2);
        M5Dial.Display.setTextColor(modeColor);
        M5Dial.Display.drawString(String(parts[selectedPart].units, 1), 120, 120);

        M5Dial.Display.setFont(&fonts::Orbitron_Light_24);
        M5Dial.Display.setTextSize(0.5);
        M5Dial.Display.setTextColor(COLOR_TEXT);
        String partInfo = String(selectedPart + 1) + " / " + String(partCount);
        M5Dial.Display.drawString(partInfo, 120, 160);
        M5Dial.Display.drawString("Press to GO", 120, 190);
      }
      break;

    case FULL_RETRACT:
      {
        M5Dial.Display.setFont(&fonts::Orbitron_Light_32);
        M5Dial.Display.setTextSize(1.5);
        M5Dial.Display.setTextColor(modeColor);
        M5Dial.Display.drawString("GO HOME", 120, 110);

        M5Dial.Display.setFont(&fonts::Orbitron_Light_24);
        M5Dial.Display.setTextSize(0.5);
        M5Dial.Display.setTextColor(COLOR_TEXT);
        M5Dial.Display.drawString("Press to retract", 120, 150);

        float currentPos = abs(stepper.currentPosition()) / (float)STEPS_PER_UNIT;
        M5Dial.Display.setTextSize(0.6);
        M5Dial.Display.setTextColor(TFT_DARKGREY);
        M5Dial.Display.drawString(String(currentPos, 1) + " mm", 120, 190);
      }
      break;

    case SETUP:
      M5Dial.Display.setFont(&fonts::Orbitron_Light_24);
      M5Dial.Display.setTextSize(0.8);
      M5Dial.Display.setTextColor(modeColor);
      if (setupOption == 0) {
        M5Dial.Display.drawString("SET HOME", 120, 100);
        M5Dial.Display.setTextSize(0.5);
        M5Dial.Display.setTextColor(COLOR_TEXT);
        M5Dial.Display.drawString("Press to set", 120, 130);
        M5Dial.Display.drawString("current position", 120, 155);
      } else {
        M5Dial.Display.drawString("SAVE PART", 120, 100);
        M5Dial.Display.setTextSize(0.5);
        M5Dial.Display.setTextColor(COLOR_TEXT);
        float currentPos = abs(stepper.currentPosition()) / (float)STEPS_PER_UNIT;
        M5Dial.Display.drawString(String(currentPos, 1) + " mm", 120, 130);
        M5Dial.Display.drawString("Press to save", 120, 155);
      }
      M5Dial.Display.setTextSize(0.4);
      M5Dial.Display.setTextColor(TFT_DARKGREY);
      M5Dial.Display.drawString("Turn to toggle", 120, 200);
      break;
  }
}

void updateDisplay() {
  if (isDispensing) {
    float currentUnits = abs(stepper.currentPosition() - homeOffset) / (float)STEPS_PER_UNIT;
    float targetUnits = abs(stepper.targetPosition() - homeOffset) / (float)STEPS_PER_UNIT;
    float progress = 0;
    if (targetUnits > 0) {
      progress = (currentUnits / targetUnits) * 100.0;
      if (progress > 100) progress = 100;
    }

    // Clear center area
    M5Dial.Display.fillCircle(120, 120, 90, COLOR_BG);

    // Draw smooth circular progress ring
    int progressRadius = 85;
    int progressWidth = 8;
    float progressAngle = (progress / 100.0) * 360.0;

    // Draw background ring (darker)
    for (float angle = 0; angle < 360; angle += 2.0) {
      float rad = (angle - 90) * PI / 180.0;  // Start at top
      for (int w = 0; w < progressWidth; w++) {
        int r = progressRadius - progressWidth/2 + w;
        int x = 120 + r * cos(rad);
        int y = 120 + r * sin(rad);
        M5Dial.Display.drawPixel(x, y, TFT_DARKGREY);
      }
    }

    // Draw progress ring (colored)
    uint32_t progressColor = COLOR_MANUAL;  // Green for progress
    for (float angle = 0; angle < progressAngle; angle += 1.5) {
      float rad = (angle - 90) * PI / 180.0;  // Start at top
      for (int w = 0; w < progressWidth; w++) {
        int r = progressRadius - progressWidth/2 + w;
        int x = 120 + r * cos(rad);
        int y = 120 + r * sin(rad);
        M5Dial.Display.drawPixel(x, y, progressColor);
      }
    }

    // Draw progress percentage
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setFont(&fonts::Orbitron_Light_32);
    M5Dial.Display.setTextSize(2);
    M5Dial.Display.setTextColor(progressColor);
    String progressText = String((int)progress) + "%";
    M5Dial.Display.drawString(progressText, 120, 110);

    // Draw current / target
    M5Dial.Display.setFont(&fonts::Orbitron_Light_24);
    M5Dial.Display.setTextSize(0.5);
    M5Dial.Display.setTextColor(COLOR_TEXT);
    String dispText = String(currentUnits, 1) + " / " + String(targetUnits, 1);
    M5Dial.Display.drawString(dispText, 120, 150);
  }
}

void setHome() {
  homeOffset = stepper.currentPosition();
  stepper.setCurrentPosition(0);
  EEPROM.put(HOME_ADDR, homeOffset);
  EEPROM.commit();
  Serial.println("Home position set");
  currentMode = MANUAL_ADVANCE;
  drawInterface();
}

void saveCurrentAsNewPart() {
  if (partCount < MAX_PARTS) {
    float currentUnits = abs(stepper.currentPosition()) / (float)STEPS_PER_UNIT;
    if (currentUnits > 0.01) {
      Part newPart;
      sprintf(newPart.name, "Part%d", partCount + 1);
      newPart.units = currentUnits;
      parts[partCount++] = newPart;
      saveParts();
      Serial.print("Saved new part: ");
      Serial.print(newPart.name);
      Serial.print(" - ");
      Serial.print(newPart.units);
      Serial.println(" units");
    }
  }
}

void retractFull() {
  isDispensing = true;
  stepper.moveTo(0);
  Serial.println("Retracting to home...");
}

void dispensePart() {
  isDispensing = true;
  float units = parts[selectedPart].units;
  stepper.moveTo(stepper.currentPosition() + (units * STEPS_PER_UNIT));
}

void loadParts() {
  EEPROM.get(PARTS_ADDR, partCount);
  if (partCount > MAX_PARTS || partCount < 0) {
    partCount = 0;
  }
  for (int i = 0; i < partCount && i < MAX_PARTS; i++) {
    EEPROM.get(PARTS_ADDR + 4 + i * sizeof(Part), parts[i]);
  }
  Serial.print("Loaded ");
  Serial.print(partCount);
  Serial.println(" parts from EEPROM");
}

void saveParts() {
  EEPROM.put(PARTS_ADDR, partCount);
  for (int i = 0; i < partCount && i < MAX_PARTS; i++) {
    EEPROM.put(PARTS_ADDR + 4 + i * sizeof(Part), parts[i]);
  }
  EEPROM.commit();
}