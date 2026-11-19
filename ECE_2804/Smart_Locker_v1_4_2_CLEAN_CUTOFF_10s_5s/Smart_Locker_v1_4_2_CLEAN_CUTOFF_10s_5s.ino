/*
  SMART LOCKER v1.4.2 - CLEAN CUT-OFF DIAGNOSTIC
  -----------------------------------------------------------------
  Based on the team's working master v1.2. Preserves:
    - 12-key resistor-ladder keypad on A5
    - Four-digit password and the existing EEPROM format
    - Timer1 SG90 servo drive and A4 current-sensing protection
    - INT0 / D2 POWER-DOWN wake-up fallback if externally powered

  ADDED / CHANGED:
    - D13 GREEN: physically unlocked
    - D12 YELLOW: authenticated/choose action; blink while changing PIN
    - D5 RED: locked, incorrect PIN blink, and PWM inactivity fade
      (RED moved from LED demo's D11 to D5, so only THREE LEDs are needed)
    - D8 buzzer: 200-ms 1-kHz success, three 300-Hz error beeps
    - Every accepted keypad press resets inactivity timer
    - After 10 seconds idle, attempt safe auto-lock and begin red fade
    - After 5 MORE seconds, release D7 to cut the whole-locker supply
    - Any key during the fade cancels dimming (key remains usable)
    - D7 holds the external 2N7000/PMOS power latch ON
    - After the red fade, D7 goes LOW to cut the entire 9-V supply
    - With a true power cutoff, restart using the physical button
    - D2 LOW wake works only with a bypass/external power source

  D7 is RESERVED for the whole-locker power latch (not a servo-only switch).
  WARNING: TP0606 is for low-current test only; disconnect servo red power
  wire until an appropriately rated P-MOSFET and supply are installed.
  IMPORTANT: Use a suitable 5-V servo supply, not an ATmega GPIO.
  IMPORTANT: D2 requires a real active-LOW wake signal. A5 alone is not
  a guaranteed digital wake source for all keypad voltages.
  IMPORTANT: D12/D13 also serve ISP programming. LEDs through resistors
  may load ISP; disconnect during programming if programming fails.
*/

// ------------------------------------------------------------
// HARDWARE ASSIGNMENTS AND CONSTANTS
// ------------------------------------------------------------
const byte keypadPin = A5;
const byte currentPin = A4;
const byte wakePin = 2;             // INT0 active LOW on ANY key press
const byte ledPinRed = 5;        // RED is on PWM D5 (physical pin 11)
const byte ledPinYellow = 12;    // YELLOW: D12 (physical pin 18)
const byte ledPinGreen = 13;     // GREEN: D13 (physical pin 19)
const byte buzzerPin = 8;
const byte servoPin = 9;           // Timer1 OC1A

// D7 -> 12k -> 2N7000 Gate; Gate also has its 100k pulldown to GND.
// 2N7000 Drain -> TP0606 Gate, Source -> GND.
// This D7 controls the entire 9-V input upstream of the MCP1702.
const byte mainPowerHoldPin = 7;  // ATmega328P DIP physical pin 13

const unsigned long SLEEP_TIMEOUT_MS = 10000UL; // DIAGNOSTIC: 10 seconds
const unsigned long DIM_DURATION_MS = 5000UL;   // DIAGNOSTIC: 5 seconds
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
    Serial.println(F("First boot: setting default password."));
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
        Serial.println(F("SAFETY FAULT: reversal also overcurrent. PWM OFF!"));
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
        Serial.print(F("BLOCKED! Current = "));
        Serial.print(currentmA, 1);
        Serial.println(F(" mA. Reversing."));
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
// These map directly onto the LED demo's five user-visible states:
//   LOCKED = WAIT_FOR_START / ENTER_PASSWORD (red solid)
//   NEUTRAL = CHOOSE_ACTION (yellow solid)
//   UNLOCKED = UNLOCKED_WAITING (green solid)
//   CHANGING_PASSWORD = ENTER_NEW_PASSWORD (yellow blink)
//   WRONG_PASSWORD = WRONG_PASSWORD_TONE (red blink / three beeps)
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
unsigned long wrongPasswordStarted = 0;
unsigned long yellowBlinkStarted = 0;
bool errorBuzzerActive = false;
bool autoLockSafetyHold = false;

const unsigned long WRONG_PASSWORD_DURATION_MS = 2000UL;
const unsigned long YELLOW_BLINK_INTERVAL_MS = 250UL;

void resetEntry() {
  enteredPassword = 0;
  digitCount = 0;
}

void showWelcome() {
  Serial.println(F("Hello User! Press 1 to get started."));
}

void startPasswordEntry() {
  resetEntry();
  uiState = ENTER_PASSWORD;
  Serial.println(F("Type your 4-digit password:"));
}

void handleDigits(char key, bool isNewPassword) {
  if (key < '0' || key > '9') {
    Serial.println(F("Please use numeric keys for a 4-digit password."));
    return;
  }

  enteredPassword = enteredPassword * 10 + (key - '0');
  digitCount++;
  Serial.print('*');  // Mask the password.
  if (digitCount < 4) return;
  Serial.println();

  if (isNewPassword) {
    savePassword(enteredPassword);
    uiState = CHOOSE_ACTION; // Match LED demo: return to NEUTRAL
    resetEntry();
    tone(buzzerPin, 1000, 200);
    Serial.println(F("PASSWORD CHANGED! Return to action selection."));
    Serial.println(F("Press '*' to UNLOCK or '#' to CHANGE PASSWORD."));
    return;
  }

  if (enteredPassword == savedPassword) {
    uiState = CHOOSE_ACTION;
    tone(buzzerPin, 1000, 200);
    Serial.println(F("CORRECT PASSWORD!"));
    Serial.println(F("Press '*' to UNLOCK or '#' to CHANGE PASSWORD."));
  } else {
    uiState = WRONG_PASSWORD_TONE;
    wrongPasswordStarted = millis();
    errorBuzzerActive = false;
    noTone(buzzerPin);
    Serial.println(F("WRONG PASSWORD! Three warning beeps."));
  }
  resetEntry();
}

void handleKey(char key) {
  switch (uiState) {
    case WAIT_FOR_START:
      if (key == '1') {
        Serial.println(lockerUnlocked ? F("Locker state: UNLOCKED") : F("Locker state: LOCKED"));
        startPasswordEntry();
      } else {
        Serial.println(F("Press 1 to get started."));
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
          Serial.println(F("Locker is now UNLOCKED. Press '*' to lock."));
        } else {
          Serial.println(F("UNLOCK FAILED. Clear obstruction before trying again."));
        }
      } else if (key == '#') {
        uiState = ENTER_NEW_PASSWORD;
        yellowBlinkStarted = millis();
        resetEntry();
        Serial.println(F("CHANGING PASSWORD... Enter a NEW 4-digit PIN:"));
      } else {
        Serial.println(F("Invalid key. Use '*' to unlock or '#' to change password."));
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
          Serial.println(F("Locker is now LOCKED."));
          showWelcome();
        } else {
          Serial.println(F("LOCK FAILED. Locker remains logically UNLOCKED."));
          Serial.println(F("Clear obstruction; press '*' to retry."));
        }
      } else {
        Serial.println(F("Press '*' to lock your locker."));
      }
      break;

    case WRONG_PASSWORD_TONE:
      // The warning lasts 2 seconds; ignore keypad commands here.
      // Valid presses still count as user activity in the main loop.
      break;
  }
}

void serviceWrongPasswordFeedback() {
  if (uiState != WRONG_PASSWORD_TONE) return;

  unsigned long elapsed = millis() - wrongPasswordStarted;
  if (elapsed >= WRONG_PASSWORD_DURATION_MS) {
    noTone(buzzerPin);
    errorBuzzerActive = false;
    uiState = WAIT_FOR_START; // Match demo: back to LOCKED
    Serial.println(F("Locker is LOCKED. Press 1 to retry."));
    return;
  }

  // Three low beeps: 0-300, 600-900, 1200-1500 milliseconds.
  bool shouldBeep = (elapsed < 300UL) ||
                    (elapsed >= 600UL && elapsed < 900UL) ||
                    (elapsed >= 1200UL && elapsed < 1500UL);
  if (shouldBeep && !errorBuzzerActive) {
    tone(buzzerPin, 300);
    errorBuzzerActive = true;
  } else if (!shouldBeep && errorBuzzerActive) {
    noTone(buzzerPin);
    errorBuzzerActive = false;
  }
}

// ------------------------------------------------------------
// POWER MANAGEMENT + 3-LED STATUS FEEDBACK
// ------------------------------------------------------------
enum PowerState { AWAKE, DIMMING };
PowerState powerState = AWAKE;
unsigned long lastActivity = 0;
unsigned long dimStarted = 0;

void userActivity() {
  lastActivity = millis();
  if (powerState == DIMMING) {
    powerState = AWAKE;
    Serial.println(F("Dimming canceled by keypad press. Locker AWAKE."));
  }
}

// LOW-level INT0 on D2; disable the interrupt until the next sleep.
void wakeUpISR() {
  EIMSK &= ~(1 << INT0);
}

// Existing direct-register POWER-DOWN technique from master v1.2.
void enterSleepMode() {
  byte previousADC = ADCSRA;
  ADCSRA &= ~(1 << ADEN);

  noInterrupts();
  attachInterrupt(digitalPinToInterrupt(wakePin), wakeUpISR, LOW);
  SMCR = (1 << SM1) | (1 << SE);

  // If a key is already held, don't go to sleep. Otherwise enable
  // interrupts immediately before SLEEP to avoid missing the wake event.
  if (digitalRead(wakePin) == HIGH) {
    asm volatile("sei\n\tsleep\n\t" ::: "memory");
    noInterrupts();
  }

  SMCR &= ~(1 << SE);
  detachInterrupt(digitalPinToInterrupt(wakePin));
  ADCSRA = previousADC;
  interrupts();
}

// Called when the 2-minute inactivity period expires.
// We LOCK BEFORE DIMMING so that the red LED honestly means LOCKED.
// Any failed auto-lock prevents sleep for safety.
bool prepareForDimming() {
  if (servoSafetyFault || autoLockSafetyHold) return false;

  if (lockerUnlocked) {
    Serial.println(F("2 minutes idle: attempting safe AUTO-LOCK..."));
    if (!moveServoSafely(LOCKED_PWM)) {
      Serial.println(F("AUTO-LOCK FAILED: remain AWAKE; inspect obstruction."));
      autoLockSafetyHold = true;
      lastActivity = millis();
      tone(buzzerPin, 600, 300);
      return false;
    }
    lockerUnlocked = false;
    Serial.println(F("AUTO-LOCK completed without sustained overcurrent."));
  }

  // Once logically locked, leave the previous menu/password state.
  uiState = WAIT_FOR_START;
  resetEntry();
  noTone(buzzerPin);
  errorBuzzerActive = false;
  return true;
}

void goToPowerDown() {
  // Never cut power when logically unlocked or after a servo fault.
  if (lockerUnlocked || servoSafetyFault || autoLockSafetyHold) {
    powerState = AWAKE;
    lastActivity = millis();
    return;
  }

  // No Serial.print/flush in the critical shutdown path.
  analogWrite(ledPinRed, 0);
  digitalWrite(ledPinYellow, LOW);
  digitalWrite(ledPinGreen, LOW);
  noTone(buzzerPin);
  disableServoPWM();

  // Do not allow an interrupt or sleep wake-up to reassert the latch.
  noInterrupts();
  digitalWrite(mainPowerHoldPin, LOW);

  // The PMOS should now remove supply. Never set D7 HIGH again
  // until the ATmega restarts after a fresh pushbutton press.
  while (true) { }
}

void servicePowerManagement() {
  if (servoSafetyFault || autoLockSafetyHold) return;
  unsigned long now = millis();

  if (powerState == AWAKE && now - lastActivity >= SLEEP_TIMEOUT_MS) {
    if (!prepareForDimming()) return;
    powerState = DIMMING;
    dimStarted = millis();
    Serial.println(F("10 seconds idle: red LED DIMMING for 5 seconds..."));
    return;
  }

  if (powerState == DIMMING && now - dimStarted >= DIM_DURATION_MS) {
    goToPowerDown();
  }
}

void serviceIndicatorLEDs() {
  unsigned long now = millis();
  bool redOn = false;
  bool yellowOn = false;
  bool greenOn = false;
  int redBrightness = 0;

  // A fault is NOT a normal LOCKED state; distinguish it by red flashing.
  if (servoSafetyFault) {
    redOn = ((now / 250UL) % 2UL == 0UL);
  } else if (powerState == DIMMING) {
    unsigned long elapsed = now - dimStarted;
    if (elapsed > DIM_DURATION_MS) elapsed = DIM_DURATION_MS;
    redBrightness = 255 - (int)(255UL * elapsed / DIM_DURATION_MS);
  } else {
    switch (uiState) {
      case WAIT_FOR_START:
      case ENTER_PASSWORD:
        redOn = true;
        break;
      case CHOOSE_ACTION:
        yellowOn = true;
        break;
      case ENTER_NEW_PASSWORD:
        yellowOn = ((now - yellowBlinkStarted) /
                     YELLOW_BLINK_INTERVAL_MS) % 2UL == 0UL;
        break;
      case UNLOCKED_WAITING:
        greenOn = true;
        break;
      case WRONG_PASSWORD_TONE:
        // Red flashes only during the three low-pitched beeps.
        redOn = errorBuzzerActive;
        break;
    }
  }

  digitalWrite(ledPinGreen, greenOn ? HIGH : LOW);
  digitalWrite(ledPinYellow, yellowOn ? HIGH : LOW);
  analogWrite(ledPinRed, redBrightness > 0 ? redBrightness :
              (redOn ? 255 : 0));
}

// ------------------------------------------------------------
// INITIALIZATION AND MAIN LOOP
// ------------------------------------------------------------
void setup() {
  // Establish the self-hold signal as the very first operation. Keep the
  // start button pressed until this output has had time to go HIGH.
  digitalWrite(mainPowerHoldPin, HIGH); // Preload HIGH before enabling output.
  pinMode(mainPowerHoldPin, OUTPUT);

  Serial.begin(9600);
  pinMode(keypadPin, INPUT);
  pinMode(currentPin, INPUT);
  pinMode(wakePin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledPinRed, OUTPUT);
  pinMode(ledPinYellow, OUTPUT);
  pinMode(ledPinGreen, OUTPUT);

  loadPassword();
  setupServoTimer1();
  lastActivity = millis();
  serviceIndicatorLEDs();
  Serial.println(F("Smart Locker v1.4.2: D7 latch diagnostic enabled."));
  Serial.println(F("10 seconds inactivity, 5 seconds red LED dimming."));
  showWelcome();
}

void loop() {
  if (servoSafetyFault) {
    // No more servo or UI commands after unsafe reversal.
    // Flash red; inspect/restart only after removing obstruction.
    serviceIndicatorLEDs();
    return;
  }

  // Keypad events replace the demo's Serial.read() commands.
  char key = pollKeypadEvent();
if (key != '\0') {
    tone(buzzerPin, 1500, 60);  // TEST: beep for each recognized key
    userActivity();
    handleKey(key);
}

  serviceWrongPasswordFeedback();
  servicePowerManagement();
  serviceIndicatorLEDs();
}
