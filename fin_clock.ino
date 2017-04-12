#include <Stepper.h>

// stepper properties
#define STEPS 32
#define ONE_REVOLUTION 2048
#define SPEED_FAST 1000
#define SPEED_MEDIUM 500
#define SPEED_LOW 250
#define SPEED_VERY_SLOW 100

// button properties
#define BUTTON_FORWARD D5
#define BUTTON_BACK D6

//misc
#define NUMBER_OF_DAYS 30
#define EERPROM_ADDRESS 0
#define THRESHOLD_IDLE 15

Stepper stepper(STEPS, D4, D2, D3, D1);
int dayArray[NUMBER_OF_DAYS];
int currentDayNumber = 12;

unsigned long timeStamp = -1;

// TCP connection code
TCPServer server = TCPServer(1337);
TCPClient client;

void setup() {
  // setup serial communication (for debugging)
  Serial.begin(9600);

  // setup stepper pins
  for(int i = 1; i <= 4; i++) {
    pinMode(i, OUTPUT);
  }

  // set button pins
  pinMode(BUTTON_FORWARD, INPUT);
  pinMode(BUTTON_BACK, INPUT);

  // create array with day intervals
  for(int i = 0; i < NUMBER_OF_DAYS; i++) {
    int intervalSize = ONE_REVOLUTION / NUMBER_OF_DAYS;
    dayArray[i] = i != NUMBER_OF_DAYS - 1 ? ((i * intervalSize) + intervalSize) : 2048;
  }

  // set stepper speed and set initial postion
  stepper.setSpeed(SPEED_MEDIUM);
  goToDayNumber(currentDayNumber);

  // TCP and cloud functions
  Particle.function("command", executeCommand);
  server.begin();
}

void loop() {
  // methods used for initial calibration!
  if(Serial.available() > 0) {
    executeCommand(Serial.readString());
  }

  // set client when availble
  if(!client.connected()) {
    Serial.println("Waiting for client...");
    client = server.available();
    delay(2000);
  }

  // make sure not to do something is not position has been set
  if(getPosition() == -1) {
    Serial.println("No start position is set!");
    delay(2000);
    return;
  }

  // if clock has been idle for more than THRESHOLD_IDLE seconds
  if(timeStamp != -1 && (millis() - timeStamp) > (THRESHOLD_IDLE * 1000)) {
    goToDayNumber(currentDayNumber);
    timeStamp = -1;
  }

  // when forward button is being pushed
  if(digitalRead(BUTTON_FORWARD) == HIGH) {
    int stepsTaken = 0;

    while(digitalRead(BUTTON_FORWARD) == HIGH) {
      stepper.step(-1);
      stepsTaken++;
    }

    int newPosition = (getPosition() + stepsTaken) % ONE_REVOLUTION;

    server.println(getDayNumberFromPosition(newPosition));
    setPosition(newPosition);
    timeStamp = millis();
  }

  // when back button is being pushed
  if(digitalRead(BUTTON_BACK) == HIGH) {
    int stepsTaken = 0;

    while(digitalRead(BUTTON_BACK) == HIGH) {
      stepper.step(1);
      stepsTaken++;
    }

    int difference = getPosition() - stepsTaken;
    int newPosition;

    if(difference >= 0) {
      newPosition = difference;
    } else {
      newPosition = ONE_REVOLUTION - ((stepsTaken - getPosition()) % ONE_REVOLUTION);
    }

    server.println(getDayNumberFromPosition(newPosition));
    setPosition(newPosition);
    timeStamp = millis();
  }
}

/********** HELPER METHODS **********/
void setPosition(short value) {
  if(value < 0 || value > ONE_REVOLUTION) {
    Serial.print("Position out of bounds! Position: ");
    Serial.println(value);
    return;
  }

  EEPROM.put(EERPROM_ADDRESS, value);
  Serial.print("New position is set to: ");
  Serial.println(value);
}

short getPosition() {
  short position;
  EEPROM.get(EERPROM_ADDRESS, position);

  if(position == 0xFFFF) position = -1;

  return position;
}

void goToPosition(int to, int from) {
  //logically it would be the other way around, but negative numbers = forward!
  stepper.step(from - to);
  setPosition(to);
}

int getDayNumberFromPosition(int stepPosition) {
  int day = - 1;

  for(int i = 0; i < NUMBER_OF_DAYS; i++) {
    if(stepPosition >= 0 && stepPosition <= dayArray[0]) {
      day = 1;
      break;
    }

    if(stepPosition > dayArray[i - 1] && stepPosition <= dayArray[i]) {
      day = i + 1;
      break;
    }
  }

  return day;
}

void goToDayNumber(int dayNumber) {
  int offset = (ONE_REVOLUTION / NUMBER_OF_DAYS) / 2;
  int currentPosition = getPosition();

  if(dayNumber < 0 || dayNumber > 30) {
    Serial.println("Cannot go to day – day is not in interval!");
    return;
  }

  if(dayNumber == 1) {
    int newPosition = offset;
    goToPosition(newPosition, currentPosition);
  } else {
    int newPosition = dayArray[dayNumber - 1] - offset;
    goToPosition(newPosition, currentPosition);
  }

  server.println(dayNumber);
  Serial.print("Current week: ");
  Serial.println(dayNumber);
}

int executeCommand(String command) {
  char commandType = command.charAt(0);
  int value = command.substring(1).toInt();
  int res = 1;

  switch(commandType) {
    case 'S':
      setPosition((short) value);
      break;
    case 'R':
      stepper.step(-1 * value);
      break;
    case 'C':
      EEPROM.clear();
      break;
    case 'D':
      goToDayNumber(value);
      break;
    case 'T':
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("SSID: ");
      Serial.println(WiFi.SSID());
      break;
    default:
      res = -1;
      break;
  }

  return res;
}
