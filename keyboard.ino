#include <Bounce.h>

#define CHANNEL 1
#define MOD_WHEEL_CC 1

#define NUM_KEY_DOWN_PINS 8
#define NUM_OUTPUT_PINS 8
#define NUM_MIDI_PINS 24 /// key down pins + key up pins + output pins
#define NUM_KEYS 61

#define BOTTOM_KEY_BASE_MIDI 36 

#define KEY_STATE_PRESSED false
#define KEY_STATE_UNPRESSED true

/*
  ANALOG BUTTONS
*/

class AnalogButton {
  
  uint8_t buttonPin;
  // Last value of analog button
  int lastVal;
  // "Current" value of analog button
  int val;
  // Hysteresis - Range in which "val" can be such that it doesn't count as changed
  // e.g. val = 30, newVal = 32, and range = 5 means that val doesn't change
  int range;
  // Time the button state last changed - ms
  uint32_t tLastChange;
  // Debouncing - ms
  const uint32_t tDelay = 5;

  bool isFallingEdge;
  bool isRisingEdge;

  AnalogButton(uint8_t buttonPin_) {
    buttonPin = buttonPin_;
    millis();
  }

  void update() {
    int newVal = analogRead(buttonPin);
    bool isOutOfRange = newVal > val + range || newVal < val - range;
    bool isDebounced = millis() - tLastChange > tDelay;
    if (isOutOfRange && isDebounced) {
      isFallingEdge = newVal < val;
      isRisingEdge = newVal > val;
      lastVal = val;
      val = newVal;
      tLastChange = millis();
    } else {
      isFallingEdge = false;
      isRisingEdge = false;
    }
  }

  int getLastVal() {
    return lastVal;
  }

  int getVal() {
    return val;
  }

  bool onFallingEdge() {
    return isFallingEdge;
  }

  bool onRisingEdge() {
    return isRisingEdge;
  }
};

/*
  END ANALOG BUTTONS
*/

/*
  PINS
*/

// Pins which, when we write to them, will produce an output on 
// another pin (if the key corresponding to that pin is down).
// This is ordered lowest note -> highest note
uint8_t keyDownPins[NUM_KEY_DOWN_PINS] = {
  0, 2, 4, 7, 6, 14, 16, 18
};

uint8_t keyStartPins[NUM_KEY_DOWN_PINS] = {
  1, 3, 5, 8, 13, 15, 17, 19
};

// Pins which produce an output when a write pin is written to.
// This is ordered lowest note -> highest note
uint8_t outputPins[NUM_OUTPUT_PINS] = {
  9, 10, 11, 12, 20, 21, 22, 23
};

#define MOD_WHEEL_PIN A10
#define PITCH_WHEEL_PIN A11
#define OCTAVE_BUTTON_PIN A14

/*
  END PINS
*/

/*
  STATE
*/

// Keeps track of the "up" or "down" state for the switches for each key
bool lastKeyDownState[NUM_KEYS];
bool lastKeyStartState[NUM_KEYS];

// Last time the start switch for a key transitioned from UP -> DOWN
int keyStartTimes[NUM_KEYS];

// Number of octaves to shift the MIDI translation
int octaveOffset = 1;

// Distance pitch wheel is bent [-512, 511]
int pitchWheelDelta = 0;

// Distance mod wheel is bent [0, 127]
int modWheelDelta = 0;

/*
  END STATE
*/

// 24x24 array mapping (write pin, read pin) -> key
uint8_t keyMap[NUM_MIDI_PINS][NUM_MIDI_PINS];

void resetKeyState() {
  for (int i = 0; i < NUM_KEYS; i++) {
    lastKeyDownState[i] = KEY_STATE_UNPRESSED;
    lastKeyStartState[i] = KEY_STATE_UNPRESSED;
    keyStartTimes[i] = 0;
  }
}

void emptyKeyMap() {
  for (int switchPin = 0; switchPin < NUM_MIDI_PINS; switchPin++) {
    for (int outputPin = 0; outputPin < NUM_MIDI_PINS; outputPin++) {
      keyMap[switchPin][outputPin] = 0;
    }
  }
}

void populateKeyMap() {
  uint8_t key = 0;
  for (int i = 0; i < NUM_KEY_DOWN_PINS; i++) {
    uint8_t keyDownPin = keyDownPins[i];
    uint8_t keyStartPin = keyStartPins[i];
    for (int j = 0; j < NUM_OUTPUT_PINS; j++) {
      uint8_t outputPin = outputPins[j];
      keyMap[keyDownPin][outputPin] = key;
      keyMap[keyStartPin][outputPin] = key;
      key++;
    }
  }
}

void setupSwitchPins() {
  for (int i = 0; i < NUM_KEY_DOWN_PINS; i++) {
    uint8_t keyDownPin = keyDownPins[i];
    pinMode(keyDownPin, INPUT_PULLDOWN);

    uint8_t keyStartPin = keyStartPins[i];
    pinMode(keyStartPin, INPUT_PULLDOWN);
  }
}

void setupOutputPins() {
  for (int i = 0; i < NUM_OUTPUT_PINS; i++) {
    uint8_t outputPin = outputPins[i];
    pinMode(outputPin, OUTPUT);
    digitalWrite(outputPin, LOW);
  }
}

void setupControlPins() {
  pinMode(A10, INPUT);
  pinMode(A11, INPUT);
  pinMode(A14, INPUT);
}

uint8_t getKey(uint8_t switchPin, uint8_t outputPin) {
  /**
    Get key number from the switch pin and output pin (0-60)
  */
  return keyMap[switchPin][outputPin];
}

uint8_t getMidi(uint8_t key) {
  return BOTTOM_KEY_BASE_MIDI + key + 12*octaveOffset;
}

double VEL_C = 30.;
double VEL_A = 127. - VEL_C;
double MEDIUM_DT = 16.;
double MEDIUM_VEL = 85.;
double VEL_B = -(1./MEDIUM_DT) * log((MEDIUM_VEL - VEL_C) / VEL_A);

uint8_t getVelocity(int start_time, int end_time) {
  /**
    Return a velocity value given this curve, in range [1, 127]:
    dt = end_time - start_time
    vel = A * e^(B*dt) + C
  */
  int dt = end_time - start_time;
  int vel = VEL_A * exp(-VEL_B*dt) + VEL_C;
  // Neither of these cases should happen
  if (vel > 127) {
    return 127;
  } else if (vel < VEL_C) {
    return VEL_C;
  }
  // Serial.println(dt);
  // Serial.println(vel);
  return vel;
}

void busyWaitForPinTransition(int pin, int val) {
  // uint32_t t = micros();
  while (digitalRead(pin) != val) {}
  // Serial.println(micros() - t);
}

int pitchWheelDeadZone = 10;
int pitchWheelHysteresis = 2;

void updatePitchBend() {
  // [0, 1023] -> [-512, 511]
  int delta = analogRead(PITCH_WHEEL_PIN) - 512;
  if (abs(delta) > pitchWheelDeadZone && abs(pitchWheelDelta - delta) > pitchWheelHysteresis) {
    usbMIDI.sendPitchBend(-8*delta, CHANNEL);
    pitchWheelDelta = delta;
  } else if (abs(delta) <= pitchWheelDeadZone && abs(pitchWheelDelta) > pitchWheelDeadZone) {
    usbMIDI.sendPitchBend(0, CHANNEL);
    pitchWheelDelta = 0;
  }
}

void updateModulation() {
  // [0, 1023] -> [0, 127]
  int delta = analogRead(MOD_WHEEL_PIN) / 8;
  if (delta != modWheelDelta) {
    usbMIDI.sendControlChange(MOD_WHEEL_CC, 127-delta, CHANNEL);
    modWheelDelta = delta;
  }
}

void setup() {

  emptyKeyMap();
  populateKeyMap();

  setupSwitchPins();
  setupOutputPins();
  setupControlPins();
  resetKeyState();

}

void loop() {
  updatePitchBend();
  updateModulation();

  // scan
  for (int i = 0; i < NUM_OUTPUT_PINS; i++) {
    uint8_t outputPin = outputPins[i];
    // write to output pin
    digitalWrite(outputPin, HIGH);
    busyWaitForPinTransition(outputPin, HIGH);
    delayMicroseconds(10);
    for (int j = 0; j < NUM_KEY_DOWN_PINS; j++) {
      uint8_t keyStartPin = keyStartPins[j];
      uint8_t keyDownPin = keyDownPins[j];

      // read velocity and key down pins
      uint8_t keyStartVal = digitalRead(keyStartPin);
      uint8_t keyDownVal = digitalRead(keyDownPin);

      uint8_t key = getKey(keyStartPin, outputPin);
      // uint8_t key = getKey(keyDownPin, outputPin);

      // detect velocity pin changes
      if (keyStartVal == HIGH && lastKeyStartState[key] == KEY_STATE_UNPRESSED) {
        // Serial.print("vel: ");
        // Serial.println(key);
        // key begins to be pressed down
        keyStartTimes[key] = millis();
        lastKeyStartState[key] = KEY_STATE_PRESSED;
      } else if (keyStartVal == LOW && lastKeyStartState[key] == KEY_STATE_PRESSED) {
        // key fully lifted
        lastKeyStartState[key] = KEY_STATE_UNPRESSED;
      }
      
      // detect key down pin changes 
      if (keyDownVal == HIGH && lastKeyDownState[key] == KEY_STATE_UNPRESSED) {
        // Serial.print("dwn: ");
        // Serial.println(key);
        // key fully pressed
        usbMIDI.sendNoteOn(getMidi(key), getVelocity(keyStartTimes[key], millis()), CHANNEL);
        lastKeyDownState[key] = KEY_STATE_PRESSED;
      } else if (keyDownVal == LOW && lastKeyDownState[key] == KEY_STATE_PRESSED) {
        // key begins to be lifted
        usbMIDI.sendNoteOff(getMidi(key), 0, CHANNEL);
        lastKeyDownState[key] = KEY_STATE_UNPRESSED;
      }
    }
    digitalWrite(outputPin, LOW);
  }

}

