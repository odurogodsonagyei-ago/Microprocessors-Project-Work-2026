#include <LiquidCrystal.h>
#include <EEPROM.h>

//LCD 
LiquidCrystal lcd(12, 11, 5, 4, 3, 6);

//COMPONENT PINS 
#define TEMP_PIN A0
#define POWER_BTN 2
#define READ_BTN 10
#define BUZZER 7
#define RED_LED 9
#define GREEN_LED 8
#define MENU_UP_BTN A1
#define MENU_DOWN_BTN A2
#define BACK_BTN A3  

// SYSTEM STATE MACHINE 
enum SystemState {
  STATE_OFF,
  STATE_READ,
  STATE_MENU,
  STATE_SUBMENU,
  STATE_TREND
};

SystemState currentState = STATE_OFF;


//Variable declarations
//  Menu mode var
int menuIndex = 0;
bool useFahrenheit = false;
int editStage = 0;
int historyViewIndex = 0;
bool confirmReset = true;

//  Alert var
float alertHigh = 38.0;
float alertLow  = 35.0;

//  Timing var
unsigned long lastDebounce = 0; //for non-blocking timing
unsigned long trendStartTime = 0;//
unsigned long systemStartTime = 0;

//  Temperature var
float tempReadings[5];
int readingCount = 0;

//  History Structure to display read temp history
#define HISTORY_SIZE 10

struct HistoryRecord {
  float temp;
  unsigned long timestamp;
};

HistoryRecord history[HISTORY_SIZE];
int historyCount = 0;


//Setup fxn
//hardware initialisation
void setup() {

  pinMode(BUZZER, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  pinMode(POWER_BTN, INPUT);
  pinMode(READ_BTN, INPUT_PULLUP);
  pinMode(MENU_UP_BTN, INPUT_PULLUP);
  pinMode(MENU_DOWN_BTN, INPUT_PULLUP);
  pinMode(BACK_BTN, INPUT_PULLUP);

  lcd.begin(16,2);
  loadHistory();

  lcd.print("System OFF");
}


//Loop
void loop() {

  handlePower();//Power on/off

  switch(currentState) {//switch states

    case STATE_OFF:
      break;

    case STATE_READ:
      handleReadMode();
      break;

    case STATE_MENU:
      handleMenu();
      break;

    case STATE_SUBMENU:
      handleSubMenu();
      break;

    case STATE_TREND:
      handleTrendState();
      break;
  }
}

//  Power change between off and read mode
void handlePower() {

  if(digitalRead(POWER_BTN) == HIGH) {

    delay(200);

    if(currentState == STATE_OFF) {
      currentState = STATE_READ;
      systemStartTime = millis();
      lcd.clear();
      lcd.print("System ON");
      delay(500);
      lcd.clear();
      lcd.print("Read Mode");
    }
    else {
      currentState = STATE_OFF;
      lcd.clear();
      lcd.print("System OFF");
    }
  }
}

//  READ MODE 
void handleReadMode() {

  if(digitalRead(MENU_UP_BTN) == LOW) {//open menu mode
    delay(150);
    currentState = STATE_MENU;
    menuIndex = 0;
    
    //  turn off alerts in read mode
    turnOffAlerts();
    
    showMenu();
    return;
  }

  if(digitalRead(READ_BTN) == LOW) {//Temp reading
    delay(150);
    float temp = readTemperature();
	
    //store temp reading in given raange
    if(temp >= 30 && temp <= 43) {
      tempReadings[readingCount] = temp;
      saveToHistory(temp);
      showStatus(temp);
      readingCount++;

      if(readingCount == 5) {
        startTrendAnalysis();
      }
    }
    else {
      lcd.clear();
      lcd.print("INVALID");
    }
  }
}

//  Trend  disply after 5 readings 
void startTrendAnalysis() {

  noTone(BUZZER);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  float change = tempReadings[4] - tempReadings[0];

  lcd.clear();
  if(change > 0.2) lcd.print("Rising");
  else if(change < -0.2) lcd.print("Falling");
  else lcd.print("Stable");

  lcd.setCursor(0,1);
  lcd.print("Trend");

  trendStartTime = millis();
  currentState = STATE_TREND;
}

void handleTrendState() {
//go back to read mode after trend display
  if(millis() - trendStartTime >= 5000) {
    readingCount = 0;
    lcd.clear();
    lcd.print("Read Mode");
    currentState = STATE_READ;
  }
}

//  System menu
void showMenu() {

  lcd.clear();

  if(menuIndex == 0) lcd.print(">History");
  if(menuIndex == 1) lcd.print(">Edit Alerts");
  if(menuIndex == 2) lcd.print(">Switch C/F");
  if(menuIndex == 3) lcd.print(">Factory Reset");

  lcd.setCursor(0,1);
  lcd.print("Read=Select");
}

void handleMenu() {

  //  Alert off in menu mode
  //shows alerts and threshholds and values in chosen units
  turnOffAlerts();
//button fxns in menu mode
  if(digitalRead(MENU_UP_BTN) == LOW) {
    delay(150);
    menuIndex--;
    if(menuIndex < 0) menuIndex = 3;
    showMenu();
  }

  if(digitalRead(MENU_DOWN_BTN) == LOW) {
    delay(150);
    menuIndex++;
    if(menuIndex > 3) menuIndex = 0;
    showMenu();
  }

  if(digitalRead(READ_BTN) == LOW) {
    delay(150);
    currentState = STATE_SUBMENU;
    executeMenu();
  }

  if(digitalRead(BACK_BTN) == LOW) {
    delay(150);
    lcd.clear();
    lcd.print("Read Mode");
    currentState = STATE_READ;
  }
}


//  SUBMENU 
void executeMenu() {

  if(menuIndex == 0) showHistory();
  if(menuIndex == 1) { editStage = 0; showAlertEdit(); }
  if(menuIndex == 2) showUnit();
  if(menuIndex == 3) showFactory();
}

void handleSubMenu() {

  //  turn off alert in sub menu
  turnOffAlerts();

  //  HISTORY 
  if(menuIndex == 0) {

    if(digitalRead(MENU_UP_BTN) == LOW) {
      delay(150);
      historyViewIndex--;
      if(historyViewIndex < 0)
        historyViewIndex = historyCount - 1;
      showHistory();
    }

    if(digitalRead(MENU_DOWN_BTN) == LOW) {
      delay(150);
      historyViewIndex++;
      if(historyViewIndex >= historyCount)
        historyViewIndex = 0;
      showHistory();
    }
  }

  //  EDIT ALERTS 
  if(menuIndex == 1) {

    if(digitalRead(MENU_UP_BTN) == LOW) {
      delay(150);
      if(editStage == 0) alertLow += 0.1;
      else alertHigh += 0.1;

      //call alert safety to prevent cross margining
      enforceAlertSafety();
      showAlertEdit();
    }

    if(digitalRead(MENU_DOWN_BTN) == LOW) {
      delay(150);
      if(editStage == 0) alertLow -= 0.1;
      else alertHigh -= 0.1;

      enforceAlertSafety();
      showAlertEdit();
    }

    if(digitalRead(READ_BTN) == LOW) {
      delay(150);
      if(editStage == 0) editStage = 1;
      else currentState = STATE_MENU;
      showAlertEdit();
    }
  }

  //  SWITCH UNIT 
  if(menuIndex == 2) {

    if(digitalRead(READ_BTN) == LOW) {
      delay(150);
      useFahrenheit = !useFahrenheit;
      currentState = STATE_MENU;
      showMenu();
    }
  }

  //  FACTORY RESET 
  if(menuIndex == 3) {

    if(digitalRead(MENU_UP_BTN) == LOW ||
       digitalRead(MENU_DOWN_BTN) == LOW) {
      delay(150);
      confirmReset = !confirmReset;
      showFactory();
    }

    if(digitalRead(READ_BTN) == LOW) {
      delay(150);

      if(confirmReset) factoryReset();
      else {
        currentState = STATE_MENU;
        showMenu();
      }
    }
  }

  if(digitalRead(BACK_BTN) == LOW) {
    delay(150);
    currentState = STATE_MENU;
    showMenu();
  }
}

//  ALERT SAFETY 
void enforceAlertSafety() {

  if(alertLow >= alertHigh - 0.1)
    alertLow = alertHigh - 0.1;

  if(alertHigh <= alertLow + 0.1)
    alertHigh = alertLow + 0.1;
}

//  DISPLAY 
//show history
void showHistory() {

  lcd.clear();

  if(historyCount == 0) {
    lcd.print("No Data");
    return;
  }

  float t = history[historyViewIndex].temp;
  unsigned long ts = history[historyViewIndex].timestamp;

  if(useFahrenheit)
    t = t * 9.0/5.0 + 32;

  lcd.print(t);
  lcd.setCursor(0,1);
  lcd.print("t:");
  lcd.print(ts);
  lcd.print("s");
}

void showAlertEdit() {
//alert edit display
  lcd.clear();
  if(editStage == 0) {
    lcd.print("LOW:");
    lcd.print(alertLow);
  } else {
    lcd.print("HIGH:");
    lcd.print(alertHigh);
  }
}

void showUnit() {
  lcd.clear();
  lcd.print(useFahrenheit ? "Fahrenheit" : "Celsius");
}

void showFactory() {
  lcd.clear();
  lcd.print("Factory Reset?");
  lcd.setCursor(0,1);
  lcd.print(confirmReset ? ">YES NO" : " YES >NO");
}

void showStatus(float temp) {
//show status and turn on elerts systems where needed
  float displayTemp = useFahrenheit ?
    temp * 9.0/5.0 + 32 : temp;

  lcd.clear();
  lcd.print(displayTemp);
  lcd.setCursor(0,1);

  if(temp > alertHigh) {
    lcd.print("HIGH TEMP!");
    tone(BUZZER,1200);
    digitalWrite(RED_LED,HIGH);
    digitalWrite(GREEN_LED,LOW);
  }
   if( temp < alertLow) {
    lcd.print("LOW TEMP!");
    tone(BUZZER,1200);
    digitalWrite(RED_LED,HIGH);
    digitalWrite(GREEN_LED,LOW);
  }
  else {
    lcd.print("NORMAL");
    noTone(BUZZER);
    digitalWrite(RED_LED,LOW);
    digitalWrite(GREEN_LED,HIGH);
  }
}

//  TEMPERATURE and TEMP AVERAGING
float readTemperature() {

  float total = 0;
  for(int i=0;i<5;i++) {
    int raw = analogRead(TEMP_PIN);
    float voltage = raw * (5.0 / 1023.0);
    total += voltage * 100;
  }
  return total/5.0;
}

//  HISTORY SAVE 
void saveToHistory(float temp) {

  unsigned long timestamp =
    (millis() - systemStartTime) / 1000;

  if(historyCount < HISTORY_SIZE) {
    history[historyCount].temp = temp;
    history[historyCount].timestamp = timestamp;
    historyCount++;
  }
  else {
    for(int i=1;i<HISTORY_SIZE;i++)
      history[i-1] = history[i];

    history[HISTORY_SIZE-1].temp = temp;
    history[HISTORY_SIZE-1].timestamp = timestamp;
  }
}

//  FACTORY RESET 
void factoryReset() {

  alertLow = 35.0;
  alertHigh = 38.0;
  useFahrenheit = false;
  historyCount = 0;

  lcd.clear();
  lcd.print("Reset Done");
  delay(1000);

  currentState = STATE_OFF;
  lcd.clear();
  lcd.print("System OFF");
}

//  EEPROM LOAD 
void loadHistory() {
  historyCount = 0;
}

// Turns off all alerts (LEDs, buzzer)
void turnOffAlerts() {
  noTone(BUZZER);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
}

