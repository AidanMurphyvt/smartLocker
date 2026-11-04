/*
  SMART LOCKER - MASTER CODE v1.2
  ------------------------------------------------------------
  Integrates the v1.1 keypad/password/EEPROM/servo/current-sense/buzzer
  features with the team's ATmega328P POWER-DOWN sleep technique.

  Workflow:
    - Every valid keypad press resets the 2-minute inactivity timer.
    - At 2 minutes idle: start a 15-second status-LED fade.
    - Any keypad press during fade cancels dimming.
    - At 2m15s idle: auto-lock (with the current-sensing safety check),
      then turn LED + servo PWM off and sleep in POWER-DOWN.
    - A keypad press wakes via INT0/D2 (needs press-detect hardware).
    - The key used to wake is consumed; RELEASE, then press '1' to start.

  PINOUT:
      A5 - existing resistor-ladder keypad (unchanged calibration)
      A4 - servo current sense (0.5-ohm low-side shunt)
      D2 - active-LOW *press detector* from keypad, for INT0 wake
      D5 - optional PWM status LED / brightness demonstration
      D8 - passive buzzer
      D9 - SG90 control signal (Timer1 OC1A)
      D7 - OPTIONAL external active-HIGH servo power switch input

  IMPORTANT HARDWARE INFORMATION:
    A5 alone cannot reliably wake on every existing key: its keypad
    voltages reach down to ~2.45 V, below the ATmega328P's guaranteed
    digital VIH minimum (3.0 V when VCC=5 V). Build a high-input-
    impedance press-detection stage that makes D2 LOW for ANY key.
    Possible test circuit: logic-level N-MOSFET GATE=A5, SOURCE=GND,
    DRAIN=D2, with D2 INPUT_PULLUP; select a FET guaranteed to switch
    at your minimum A5 key voltage. Verify it does not disturb A5 ADC.
    Do not simply short D2 to A5.

    POWER-DOWN IS NOT ZERO POWER. The ATmega and regulator still draw
    small current, and the servo still draws current if its power
    remains connected. Use an appropriately rated external high-side
    switch for servo V+ if desired; never power a servo from a GPIO.
    Zero whole-system power requires an external power latch plus
    a battery-powered wake circuit, not MCU sleep code alone.

  No #include libraries and no delay(). Servo moves still use the
  v1.1 bounded current-monitoring polling window (~500 ms), which
  means movement itself is temporarily blocking.
*/

// ------------------------------------------------------------
// HARDWARE ASSIGNMENTS AND CONSTANTS
// ------------------------------------------------------------
const byte keypadPin = A5;
const byte currentPin = A4;
const byte wakePin = 2;             // INT0 active LOW on ANY key press
const byte statusLedPin = 5;       // PWM visual status indicator only
const byte buzzerPin = 8;
const byte servoPin = 9;           // Timer1 OC1A

// OPTIONAL: set true ONLY after installing an external servo power
// switching circuit whose logic control input is active HIGH.
// This is NOT a way to drive the SG90 from a GPIO pin.
const bool USE_SERVO_POWER_SWITCH = false;
const byte servoPowerEnablePin = 7;

const unsigned long SLEEP_TIMEOUT_MS = 120000UL; // 2 minutes
const unsigned long DIM_DURATION_MS = 15000UL;   // 15 seconds
const unsigned long DEBOUNCE_MS = 25UL;

const int NUM_KEYS = 12;
const int Sm[NUM_KEYS] =
  {501, 528, 552, 577, 603, 644, 695, 747, 796, 857, 935, 1013};
const int Lg[NUM_KEYS] =
  {522, 548, 573, 597, 630, 665, 716, 767, 816, 878, 955, 1023};
const char symbols[NUM_KEYS] =
  {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};

// ------------------------------------------------------------
// EEPROM - ORIGINAL DIRECT-REGISTER APPROACH, NO LIBRARY
// ------------------------------------------------------------
void EEPROM_write(unsigned int uiAddress, unsigned char ucData) {
  while (EECR & (1 << EEPE));
  EEAR = uiAddress;
  EEDR = ucData;
  EECR |= (1 << EEMPE);
  EECR |= (1 << EEPE);
}

unsigned char EEPROM_read(unsigned int uiAddress) {
  while (EECR & (1 << EEPE));
  EEAR = uiAddress;
  EECR |= (1 << EERE);
  return EEDR;
}

int savedPassword = 0;

void savePassword(int pass) {
  EEPROM_write(1, (unsigned char)(pass / 100));
  EEPROM_write(2, (unsigned char)(pass % 100));
  savedPassword = pass;
}

void loadPassword() {
  unsigned char hi = EEPROM_read(1);
  unsigned char lo = EEPROM_read(2);
  if (hi == 255 && lo == 255) {
    Serial.println("First boot: setting default password.");
    savePassword(1234);
  } else {
    savedPassword = (int)hi * 100 + (int)lo;
  }
}

// NOTE: This preserves the original EEPROM format for compatibility.
// It is NOT encrypted or hashed (Milestone 4 requires stronger storage).

// ------------------------------------------------------------
// SERVO PWM + CURRENT BASED OBSTRUCTION DETECTION (v1.1)
// ------------------------------------------------------------
const float senseResistance = 0.5f; // 2 x 1 ohm resistors parallel
const float adcReferenceVolts = 5.0f;
const float blockThresholdmA = 200.0f; // CALIBRATE on real hardware
const unsigned long blockTimeMs = 100UL;
const unsigned long startupGraceMs = 70UL;
const unsigned long servoMoveTimeMs = 500UL;
const unsigned long reverseTimeMs = 250UL;

const unsigned int LOCKED_PWM = 4000;
const unsigned int UNLOCKED_PWM = 3000;
unsigned int lastGoodServoPWM = LOCKED_PWM;
bool servoSafetyFault = false;
bool lockerUnlocked = false;

void enableServoPower() {
  if (USE_SERVO_POWER_SWITCH) {
    digitalWrite(servoPowerEnablePin, HIGH);
  }
}

void disableServoPower() {
  if (USE_SERVO_POWER_SWITCH) {
    digitalWrite(servoPowerEnablePin, LOW);
  }
}

void enableServoPWM() {
  OCR1A = lastGoodServoPWM;
  TCCR1A |= (1 << COM1A1);
}

void disableServoPWM() {
  TCCR1A &= ~(1 << COM1A1);
  digitalWrite(servoPin, LOW);
}

void setupServoTimer1() {
  pinMode(servoPin, OUTPUT);
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1A |= (1 << COM1A1) | (1 << WGM11);
  TCCR1B |= (1 << WGM13) | (1 << WGM12) | (1 << CS11);
  ICR1 = 39999;                  // 50 Hz at 16 MHz / prescaler 8
  OCR1A = LOCKED_PWM;            // same startup assumption as v1.1
  lastGoodServoPWM = LOCKED_PWM;
}

float readServoCurrentmA() {
  int adcValue = analogRead(currentPin);
  float senseVoltage = adcValue * (adcReferenceVolts / 1023.0f);
  return (senseVoltage / senseResistance) * 1000.0f;
}

void revertServoPosition(unsigned int previousPWM) {
  OCR1A = previousPWM;
  unsigned long returnStarted = millis();
  unsigned long highCurrentStarted = 0;
  bool highCurrent = false;

  while (millis() - returnStarted < reverseTimeMs) {
    unsigned long now = millis();
    if (now - returnStarted < startupGraceMs) continue;

    float mA = readServoCurrentmA();
    if (mA > blockThresholdmA) {
      if (!highCurrent) {
        highCurrent = true;
        highCurrentStarted = now;
      } else if (now - highCurrentStarted >= blockTimeMs) {
        disableServoPWM();
        servoSafetyFault = true;
        Serial.println("SAFETY FAULT: reversal also overcurrent. PWM OFF!");
        return;
      }
    } else {
      highCurrent = false;
    }
  }
}

// Returns true if no sustained high current was observed. A true result
// does NOT prove the servo physically reached its target position.
bool moveServoSafely(unsigned int targetPWM) {
  if (servoSafetyFault) return false;
  unsigned int previousPWM = lastGoodServoPWM;
  if (targetPWM == previousPWM) return true;

  enableServoPower();
  enableServoPWM();
  OCR1A = targetPWM;

  unsigned long moveStarted = millis();
  unsigned long highCurrentStarted = 0;
  bool highCurrent = false;

  while (millis() - moveStarted < servoMoveTimeMs) {
    unsigned long now = millis();
    if (now - moveStarted < startupGraceMs) continue;
    float currentmA = readServoCurrentmA();

    if (currentmA > blockThresholdmA) {
      if (!highCurrent) {
        highCurrent = true;
        highCurrentStarted = now;
      } else if (now - highCurrentStarted >= blockTimeMs) {
        Serial.print("BLOCKED! Current = ");
        Serial.print(currentmA, 1);
        Serial.println(" mA. Reversing.");
        revertServoPosition(previousPWM);
        return false;
      }
    } else {
      highCurrent = false;
    }
  }

  lastGoodServoPWM = targetPWM;
  return true;
}

// ------------------------------------------------------------
// KEYPAD: EVENT-BASED INSTEAD OF BLOCKING WHILE LOOPS
// ------------------------------------------------------------
int candidateKeyIndex = -1;
unsigned long candidateChangedAt = 0;
bool keyAlreadyReported = false;
bool ignoreWakeKeyUntilReleased = false;

int getRawKeyIndex() {
  int sample = analogRead(keypadPin);
  for (int i = 0; i < NUM_KEYS; i++) {
    if (sample >= Sm[i] && sample <= Lg[i]) return i;
  }
  return -1;
}

char pollKeypadEvent() {
  int rawKey = getRawKeyIndex();
  unsigned long now = millis();

  // The first key press only wakes the device; wait for release.
  if (ignoreWakeKeyUntilReleased) {
    if (rawKey == -1) {
      ignoreWakeKeyUntilReleased = false;
      candidateKeyIndex = -1;
      keyAlreadyReported = false;
      candidateChangedAt = now;
    }
    return '\0';
  }

  if (rawKey != candidateKeyIndex) {
    candidateKeyIndex = rawKey;
    candidateChangedAt = now;
    // New keys are only accepted after release of the preceding key.
    if (rawKey == -1) keyAlreadyReported = false;
  }

  if (candidateKeyIndex >= 0 && !keyAlreadyReported &&
      now - candidateChangedAt >= DEBOUNCE_MS) {
    keyAlreadyReported = true;
    return symbols[candidateKeyIndex];
  }
  return '\0';
}

// ------------------------------------------------------------
// LOCKER USER INTERFACE STATE MACHINE
// ------------------------------------------------------------
enum UIState {
  WAIT_FOR_START,
  ENTER_PASSWORD,
  CHOOSE_ACTION,
  ENTER_NEW_PASSWORD,
  UNLOCKED_WAITING,
  WRONG_PASSWORD_TONE
};
UIState uiState = WAIT_FOR_START;

int enteredPassword = 0;
byte digitCount = 0;
unsigned long errorToneStarted = 0;
bool autoLockSafetyHold = false;

void resetEntry() {
  enteredPassword = 0;
  digitCount = 0;
}

void showWelcome() {
  Serial.println("Hello User! Press 1 to get started.");
}

void startPasswordEntry() {
  resetEntry();
  uiState = ENTER_PASSWORD;
  Serial.println("Type your 4-digit password:");
}

void handleDigits(char key, bool isNewPassword) {
  if (key < '0' || key > '9') {
    Serial.println("Please use numeric keys for a 4-digit password.");
    return;
  }

  enteredPassword = enteredPassword * 10 + (key - '0');
  digitCount++;
  Serial.print('*'); // Do NOT print the actual password digits.
  if (digitCount < 4) return;
  Serial.println();

  if (isNewPassword) {
    savePassword(enteredPassword);
    Serial.println("Password saved in EEPROM. Returning to welcome screen.");
    uiState = WAIT_FOR_START;
    resetEntry();
    showWelcome();
    return;
  }

  if (enteredPassword == savedPassword) {
    Serial.println("Password is correct!");
    uiState = CHOOSE_ACTION;
    Serial.println("Press '*' to UNLOCK or '#' to CHANGE PASSWORD.");
  } else {
    Serial.println("ERROR: Incorrect password.");
    tone(buzzerPin, 500);
    errorToneStarted = millis();
    uiState = WRONG_PASSWORD_TONE;
  }
  resetEntry();
}

void handleKey(char key) {
  switch (uiState) {
    case WAIT_FOR_START:
      if (key == '1') {
        Serial.println(lockerUnlocked ? "Locker state: UNLOCKED" : "Locker state: LOCKED");
        startPasswordEntry();
      } else {
        Serial.println("Press 1 to get started.");
      }
      break;

    case ENTER_PASSWORD:
      handleDigits(key, false);
      break;

    case CHOOSE_ACTION:
      if (key == '*') {
        if (moveServoSafely(UNLOCKED_PWM)) {
          lockerUnlocked = true;
          uiState = UNLOCKED_WAITING;
          Serial.println("Locker is now UNLOCKED. Press '*' to lock.");
        } else {
          Serial.println("UNLOCK FAILED. Clear obstruction before trying again.");
        }
      } else if (key == '#') {
        uiState = ENTER_NEW_PASSWORD;
        resetEntry();
        Serial.println("Type a NEW 4-digit password:");
      } else {
        Serial.println("Invalid key. Use '*' to unlock or '#' to change password.");
      }
      break;

    case ENTER_NEW_PASSWORD:
      handleDigits(key, true);
      break;

    case UNLOCKED_WAITING:
      if (key == '*') {
        if (moveServoSafely(LOCKED_PWM)) {
          lockerUnlocked = false;
          autoLockSafetyHold = false;
          uiState = WAIT_FOR_START;
          Serial.println("Locker is now LOCKED.");
          showWelcome();
        } else {
          Serial.println("LOCK FAILED. Locker remains logically UNLOCKED.");
          Serial.println("Clear obstruction; press '*' to retry.");
        }
      } else {
        Serial.println("Press '*' to lock your locker.");
      }
      break;

    case WRONG_PASSWORD_TONE:
      // Buzzer runs for 500 ms, then UI returns to password entry.
      break;
  }
}

void serviceErrorTone() {
  if (uiState == WRONG_PASSWORD_TONE &&
      millis() - errorToneStarted >= 500UL) {
    noTone(buzzerPin);
    startPasswordEntry();
  }
}

// ------------------------------------------------------------
// NEW: POWER MANAGEMENT STATE MACHINE
// ------------------------------------------------------------
enum PowerState { AWAKE, DIMMING };
PowerState powerState = AWAKE;
unsigned long lastActivity = 0;
unsigned long dimStarted = 0;

void userActivity() {
  lastActivity = millis();
  if (powerState == DIMMING) {
    powerState = AWAKE;
    analogWrite(statusLedPin, 255);
    Serial.println("Dimming canceled by keypad press. Locker AWAKE.");
  }
}

// INT0 ISR: just disable INT0 after the LOW-level wake-up interrupt.
void wakeUpISR() {
  EIMSK &= ~(1 << INT0);
}

// Uses the team's direct-register, no-include, POWER-DOWN technique.
// The D2 wake signal must stay LOW long enough for the MCU to wake.
void enterSleepMode() {
  byte previousADC = ADCSRA;
  ADCSRA &= ~(1 << ADEN); // ADC consumes no current while asleep

  noInterrupts();
  attachInterrupt(digitalPinToInterrupt(wakePin), wakeUpISR, LOW);
  SMCR = (1 << SM1) | (1 << SE); // Power-down sleep mode

  // Skip sleep if a key is ALREADY held down. Otherwise use the
  // atomic SEI/SLEEP sequence to avoid losing a new wake event.
  if (digitalRead(wakePin) == HIGH) {
    asm volatile("sei\n\tsleep\n\t" ::: "memory");
    noInterrupts();
  }

  SMCR &= ~(1 << SE);
  detachInterrupt(digitalPinToInterrupt(wakePin));
  ADCSRA = previousADC;
  interrupts();
}

void goToPowerDown() {
  // Never declare the system safe to sleep if an unlocked locker
  // could not be automatically locked.
  if (lockerUnlocked) {
    Serial.println("Inactivity expired: attempting AUTO-LOCK...");
    if (!moveServoSafely(LOCKED_PWM)) {
      Serial.println("AUTO-LOCK FAILED! Stay AWAKE; obstruction needs attention.");
      powerState = AWAKE;
      lastActivity = millis();
      analogWrite(statusLedPin, 255);
      autoLockSafetyHold = true; // Suppress automatic retries
      tone(buzzerPin, 600, 300);
      return;
    }
    lockerUnlocked = false;
    uiState = WAIT_FOR_START;
    Serial.println("AUTO-LOCK completed without sustained overcurrent.");
  }

  if (servoSafetyFault) {
    Serial.println("SAFETY FAULT: no sleep. Inspect servo and lock position.");
    powerState = AWAKE;
    autoLockSafetyHold = true;
    analogWrite(statusLedPin, 255);
    return;
  }

  // Cancel partial passwords / menu interactions. The locker is locked.
  uiState = WAIT_FOR_START;
  resetEntry();
  noTone(buzzerPin);
  analogWrite(statusLedPin, 0);
  disableServoPWM();
  disableServoPower();  // Only works with an installed external switch

  Serial.println("Locker is entering ATmega328P POWER-DOWN...");
  Serial.flush();
  enterSleepMode(); // execution resumes here after a D2 LOW wake event

  // WAKE-UP SEQUENCE
  enableServoPower();
  powerState = AWAKE;
  lastActivity = millis();
  dimStarted = 0;
  ignoreWakeKeyUntilReleased = true; // wake press is not a PIN digit
  candidateKeyIndex = -1;
  keyAlreadyReported = false;
  analogWrite(statusLedPin, 255);
  Serial.println("KEYPAD WAKE! Locker AWAKE and logically LOCKED.");
  Serial.println("Release the waking key, then press 1 to start.");
}

void servicePowerManagement() {
  if (servoSafetyFault || autoLockSafetyHold) return;
  unsigned long now = millis();

  if (powerState == AWAKE) {
    if (now - lastActivity >= SLEEP_TIMEOUT_MS) {
      powerState = DIMMING;
      dimStarted = now;
      Serial.println("2 minutes idle: DIMMING for 15 seconds...");
    }
    return;
  }

  if (powerState == DIMMING) {
    unsigned long elapsed = now - dimStarted;
    if (elapsed < DIM_DURATION_MS) {
      int brightness = 255 - (int)(255UL * elapsed / DIM_DURATION_MS);
      analogWrite(statusLedPin, brightness);
    } else {
      goToPowerDown();
    }
  }
}

// ------------------------------------------------------------
// INITIALIZATION AND MAIN LOOP
// ------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  pinMode(keypadPin, INPUT);
  pinMode(currentPin, INPUT);
  pinMode(wakePin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  pinMode(statusLedPin, OUTPUT);
  if (USE_SERVO_POWER_SWITCH) {
    pinMode(servoPowerEnablePin, OUTPUT);
    enableServoPower();
  }

  loadPassword();
  setupServoTimer1();
  analogWrite(statusLedPin, 255);
  lastActivity = millis();

  Serial.println("Smart Locker v1.2: power manager enabled.");
  Serial.println("A keypad press resets the 2-minute activity timer.");
  showWelcome();
}

void loop() {
  if (servoSafetyFault) {
    // Never attempt another servo command after unsafe reversal.
    // Reset and inspect the hardware before continuing.
    return;
  }

  // Nonblocking keypad polling allows inactivity timing to work
  // even during password entry or while waiting for menu choices.
  char key = pollKeypadEvent();
  if (key != '\0') {
    userActivity();
    handleKey(key);
  }

  serviceErrorTone();
  servicePowerManagement();
}
