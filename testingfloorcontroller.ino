#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/* ================= LCD ================= */
LiquidCrystal_I2C lcd(0x27, 20, 4);

/* ================= BUTTONS ================= */
#define BTN_UP     2
#define BTN_DOWN   3
#define BTN_ENTER  4
#define BTN_SHIFT  5

/* ================= RS485 ================= */
#define RS485_DE   6

/* ================= PROTOCOL ================= */
#define ZONE_START   0xAA
#define ZONE_END     0x55
#define FLOOR_START  0xDE
#define FLOOR_END    0xE9
#define CMD_REQ      0x80
#define CMD_CODE     0xA1

#define MAX_ZONES    4

/* ================= DATA STRUCT ================= */
struct ZoneData {
  uint8_t totalSensors;
  uint8_t vacant;
  uint8_t engaged;
  uint8_t faulty;
  uint8_t noComm;
  bool    valid;
};

ZoneData zone[MAX_ZONES];

/* ================= CONFIG ================= */
uint16_t totalFloors = 0;
uint16_t floorId     = 1;

/* ================= UI ================= */
enum UiMode { RUN_MODE, MENU_MODE };
enum MenuState { SET_TOTAL_FLOORS, SET_FLOOR_ID, SAVE_DONE };

UiMode uiMode = RUN_MODE;
MenuState menuState = SET_TOTAL_FLOORS;

/* ================= DIGIT EDIT ================= */
uint8_t digits[3] = {0, 0, 0};
uint8_t cursorPos = 0;
bool editing = false;

/* ================= BUTTON STATE ================= */
bool lastUp = HIGH, lastDown = HIGH, lastEnter = HIGH, lastShift = HIGH;

/* ================= HELPERS ================= */
bool pressed(uint8_t pin, bool &last) {
  bool cur = digitalRead(pin);
  if (cur == LOW && last == HIGH) {
    delay(30);
    last = cur;
    return true;
  }
  last = cur;
  return false;
}

uint16_t crcXor(uint8_t *buf, uint8_t len) {
  uint16_t crc = 0;
  for (uint8_t i = 0; i < len; i++) crc ^= buf[i];
  return crc;
}

void rs485Tx() { digitalWrite(RS485_DE, HIGH); }
void rs485Rx() { digitalWrite(RS485_DE, LOW); }

/* ================= EEPROM ================= */
void saveConfig() {
  EEPROM.put(0, totalFloors);
  EEPROM.put(2, floorId);
}

void loadConfig() {
  EEPROM.get(0, totalFloors);
  EEPROM.get(2, floorId);
}

/* ================= LCD ================= */
void lcdBoot() {
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("HOUSTON SYSTEM");
  lcd.setCursor(0, 2);
  lcd.print("Floor Init...");
}

void lcdRun() {
  uint16_t tv = 0, te = 0, tf = 0, tn = 0;

  for (uint8_t i = 0; i < MAX_ZONES; i++) {
    tv += zone[i].vacant;
    te += zone[i].engaged;
    tf += zone[i].faulty;
    tn += zone[i].noComm;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("F_ID=");
  lcd.print(floorId);
  lcd.print(" TZ=");
  lcd.print(MAX_ZONES);

  lcd.setCursor(0, 1);
  lcd.print("TV=");
  lcd.print(tv);
  lcd.print(" TE=");
  lcd.print(te);

  lcd.setCursor(0, 2);
  lcd.print("TF=");
  lcd.print(tf);
  lcd.print(" TN=");
  lcd.print(tn);
}

void lcdMenu(const char *label) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(label);

  lcd.setCursor(0, 2);
  lcd.print("VALUE: ");
  for (uint8_t i = 0; i < 3; i++) {
    if (i == cursorPos) lcd.print('[');
    lcd.print(digits[i]);
    if (i == cursorPos) lcd.print(']');
  }
}

/* ================= ZONE COMM ================= */
void sendZoneRequest(uint8_t zid) {
  uint8_t pkt[7];
  pkt[0] = ZONE_START;
  pkt[1] = zid;
  pkt[2] = CMD_REQ;
  pkt[3] = CMD_CODE;
  uint16_t crc = crcXor(pkt, 4);
  pkt[4] = crc & 0xFF;
  pkt[5] = crc >> 8;
  pkt[6] = ZONE_END;

  rs485Tx();
  Serial1.write(pkt, 7);
  Serial1.flush();
  rs485Rx();
}

bool readZoneResponse(uint8_t zid) {
  uint8_t buf[32];
  uint8_t idx = 0;
  unsigned long t0 = millis();

  while (millis() - t0 < 200) {
    if (Serial1.available()) {
      buf[idx++] = Serial1.read();
      if (buf[idx - 1] == ZONE_END) break;
    }
  }

  if (idx < 10 || buf[0] != ZONE_START || buf[1] != zid) {
    zone[zid - 1].noComm++;
    return false;
  }

  zone[zid - 1].totalSensors = buf[2];
  zone[zid - 1].vacant  = buf[idx - 5];
  zone[zid - 1].engaged = buf[idx - 4];
  zone[zid - 1].faulty  = buf[idx - 3];
  zone[zid - 1].noComm  = buf[idx - 2];
  zone[zid - 1].valid   = true;
  return true;
}

void pollZones() {
  for (uint8_t i = 0; i < MAX_ZONES; i++) zone[i].valid = false;
  for (uint8_t z = 1; z <= MAX_ZONES; z++) {
    sendZoneRequest(z);
    readZoneResponse(z);
  }
}

/* ================= MASTER HANDLING ================= */
void sendFloorResponse(uint8_t fid) {
  uint8_t tx[200];
  uint16_t idx = 0;
  uint8_t tv = 0, te = 0, tf = 0, tn = 0;

  tx[idx++] = FLOOR_START;
  tx[idx++] = fid;
  tx[idx++] = MAX_ZONES;

  for (uint8_t z = 0; z < MAX_ZONES; z++) {
    tx[idx++] = ZONE_START;
    tx[idx++] = z + 1;
    tx[idx++] = zone[z].totalSensors;

    for (uint8_t i = 0; i < zone[z].totalSensors; i++)
      tx[idx++] = i;

    tx[idx++] = zone[z].vacant;
    tx[idx++] = zone[z].engaged;
    tx[idx++] = zone[z].faulty;
    tx[idx++] = zone[z].noComm;
    tx[idx++] = ZONE_END;

    tv += zone[z].vacant;
    te += zone[z].engaged;
    tf += zone[z].faulty;
    tn += zone[z].noComm;
  }

  tx[idx++] = tv;
  tx[idx++] = te;
  tx[idx++] = tf;
  tx[idx++] = tn;
  tx[idx++] = FLOOR_END;

  rs485Tx();
  Serial1.write(tx, idx);
  Serial1.flush();
  rs485Rx();
}

void handleMasterRequest() {
  if (Serial1.available() < 7) return;
  uint8_t buf[7];
  Serial1.readBytes(buf, 7);
  if (buf[0] == FLOOR_START && buf[2] == CMD_REQ)
    sendFloorResponse(buf[1]);
}

/* ================= SETUP ================= */
void setup() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);
  pinMode(BTN_SHIFT, INPUT_PULLUP);
  pinMode(RS485_DE, OUTPUT);
  rs485Rx();

  Serial1.begin(19200);

  lcd.init();
  lcd.backlight();
  lcdBoot();
  delay(2000);

  loadConfig();
}

/* ================= LOOP ================= */
void loop() {

  handleMasterRequest();

  static unsigned long tPoll = 0;
  if (millis() - tPoll > 1000) {
    pollZones();
    tPoll = millis();
  }

  if (pressed(BTN_ENTER, lastEnter)) {
    if (uiMode == RUN_MODE) {
      uiMode = MENU_MODE;
      menuState = SET_TOTAL_FLOORS;
      editing = false;
    } else {
      saveConfig();
      uiMode = RUN_MODE;
    }
  }

  if (uiMode == MENU_MODE) {

    if (!editing) {
      digits[0] = digits[1] = digits[2] = 0;
      cursorPos = 0;
      editing = true;
    }

    if (pressed(BTN_UP, lastUp))   digits[cursorPos] = (digits[cursorPos] + 1) % 10;
    if (pressed(BTN_DOWN, lastDown)) digits[cursorPos] = (digits[cursorPos] + 9) % 10;
    if (pressed(BTN_SHIFT, lastShift)) cursorPos = (cursorPos + 1) % 3;

    if (pressed(BTN_ENTER, lastEnter)) {
      uint16_t val = digits[0] * 100 + digits[1] * 10 + digits[2];
      if (menuState == SET_TOTAL_FLOORS) totalFloors = val;
      if (menuState == SET_FLOOR_ID) floorId = val;
      menuState = (MenuState)(menuState + 1);
      editing = false;
    }

    lcdMenu(menuState == SET_TOTAL_FLOORS ? "TOTAL FLOORS" : "FLOOR ID");
  }
  else {
    lcdRun();
  }
}
