#include <Bounce.h>

#define CHANNEL 1

#define NUM_KEY_DOWN_PINS 8
#define NUM_OUTPUT_PINS 8
#define NUM_MIDI_PINS 24 /// key down pins + key up pins + output pins
#define NUM_KEYS 61

#define BOTTOM_KEY_BASE_MIDI 36 

#define KEY_STATE_PRESSED false
#define KEY_STATE_UNPRESSED true

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
double MEDIUM_DT = 14.;
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
  return vel;
}

void busyWaitForPinTransition(int pin, int val) {
  while (digitalRead(pin) != val) {}
}

void setup() {

  emptyKeyMap();
  populateKeyMap();

  setupSwitchPins();
  setupOutputPins();
  resetKeyState();

}

uint32_t tick = 0;

void loop() {

  // scan
  for (int i = 0; i < NUM_OUTPUT_PINS; i++) {
    uint8_t outputPin = outputPins[i];
    // write to output pin
    digitalWrite(outputPin, HIGH);
    // delay(1); // TODO: make lower?
    busyWaitForPinTransition(outputPin, HIGH);
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
        // key begins to be pressed down
        keyStartTimes[key] = millis();
        lastKeyStartState[key] = KEY_STATE_PRESSED;
      } else if (keyStartVal == LOW && lastKeyStartState[key] == KEY_STATE_PRESSED) {
        // key fully lifted
        lastKeyStartState[key] = KEY_STATE_UNPRESSED;
      }
      
      // detect key down pin changes 
      if (keyDownVal == HIGH && lastKeyDownState[key] == KEY_STATE_UNPRESSED) {
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

