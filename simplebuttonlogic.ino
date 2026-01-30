/* ================= BUTTON TEST ONLY ================= */
/* No LCD, No RS485, No EEPROM */

/* ===== BUTTON PINS ===== */
#define BTN_UP     2
#define BTN_DOWN   3
#define BTN_ENTER  4
#define BTN_SHIFT  5

/* ===== MENU STATES ===== */
enum MenuState {
  SET_TOTAL_FLOORS,
  SET_FLOOR_ID,
  DONE
};

MenuState menuState = SET_TOTAL_FLOORS;

/* ===== DIGIT EDIT ===== */
uint8_t digits[3] = {0, 0, 0};   // 000
uint8_t cursorPos = 0;
bool editing = true;

/* ===== RESULT VALUES ===== */
uint16_t totalFloors = 0;
uint16_t floorId = 0;

/* ===== BUTTON STATE ===== */
bool lastUp = HIGH;
bool lastDown = HIGH;
bool lastEnter = HIGH;
bool lastShift = HIGH;

/* ===== BUTTON HELPER ===== */
bool pressed(uint8_t pin, bool &last) {
  bool cur = digitalRead(pin);
  if (cur == LOW && last == HIGH) {
    delay(30);                  // debounce
    last = cur;
    return true;
  }
  last = cur;
  return false;
}

/* ===== PRINT UI ===== */
void printEditor() {
  Serial.print("\n> ");
  Serial.print(menuState == SET_TOTAL_FLOORS ? "SET TOTAL FLOORS: " :
               menuState == SET_FLOOR_ID     ? "SET FLOOR ID    : " :
                                                "DONE");

  Serial.print("  ");

  for (uint8_t i = 0; i < 3; i++) {
    if (i == cursorPos) Serial.print("[");
    Serial.print(digits[i]);
    if (i == cursorPos) Serial.print("]");
  }
  Serial.println();
}

/* ===== SETUP ===== */
void setup() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);
  pinMode(BTN_SHIFT, INPUT_PULLUP);

  Serial.begin(9600);
  Serial.println("\n--- BUTTON LOGIC TEST STARTED ---");
  printEditor();
}

/* ===== LOOP ===== */
void loop() {

  /* ---- UP ---- */
  if (pressed(BTN_UP, lastUp)) {
    digits[cursorPos] = (digits[cursorPos] + 1) % 10;
    printEditor();
  }

  /* ---- DOWN ---- */
  if (pressed(BTN_DOWN, lastDown)) {
    digits[cursorPos] = (digits[cursorPos] + 9) % 10;
    printEditor();
  }

  /* ---- SHIFT ---- */
  if (pressed(BTN_SHIFT, lastShift)) {
    cursorPos = (cursorPos + 1) % 3;
    printEditor();
  }

  /* ---- ENTER ---- */
  if (pressed(BTN_ENTER, lastEnter)) {

    uint16_t value =
      digits[0] * 100 +
      digits[1] * 10 +
      digits[2];

    if (menuState == SET_TOTAL_FLOORS) {
      totalFloors = value;
      Serial.print("\nTOTAL FLOORS SAVED = ");
      Serial.println(totalFloors);
      menuState = SET_FLOOR_ID;
    }
    else if (menuState == SET_FLOOR_ID) {
      floorId = value;
      Serial.print("\nFLOOR ID SAVED = ");
      Serial.println(floorId);
      menuState = DONE;
    }
    else {
      Serial.println("\n--- RESTARTING ---");
      menuState = SET_TOTAL_FLOORS;
    }

    digits[0] = digits[1] = digits[2] = 0;
    cursorPos = 0;
    printEditor();
  }
}
