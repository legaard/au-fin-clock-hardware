#include <Stepper.h>

// stepper properties
#define STEPS 32
#define ONE_REVOLUTION 2048
#define SPEED_FAST 1000
#define SPEED_MEDIUM 500
#define SPEED_LOW 250
#define SPEED_VERY_LOW 100
#define FORWARD_STEP -1
#define BACKWARD_STEP 1

// button properties
#define BUTTON_FORWARD D5
#define BUTTON_BACK D6
#define TRIG A4
#define ECHO A5

//misc
#define NUMBER_OF_DAYS 30
#define NUMBER_OF_MINUTES_12_HOURS (60 * 12) - 1
#define EERPROM_ADDRESS 0
#define THRESHOLD_IDLE 20

//ping
#define SIZE_PING_AVG 10
#define AVG_DISTANCE_TRIGGER 100

//time
#define TIME_UPDATE_INTERVAL 60

Stepper stepper(STEPS, D4, D2, D3, D1);
int dayArray[NUMBER_OF_DAYS];
int currentDayNumber;

unsigned long timeStampIdle = -1;
unsigned long timeStampTime;

// moving average
long distanceArray[SIZE_PING_AVG];

// TCP connection code
int PORT = 1337;
char MESSAGE_SEPARATOR = '#';
TCPServer server = TCPServer(PORT);
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

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(D7, OUTPUT); // built in LED
  stepper.setSpeed(SPEED_LOW);

  // create array with day intervals
  for(int i = 0; i < NUMBER_OF_DAYS; i++) {
    int intervalSize = ONE_REVOLUTION / NUMBER_OF_DAYS;
    dayArray[i] = i != NUMBER_OF_DAYS - 1 ? ((i * intervalSize) + intervalSize) : ONE_REVOLUTION;
  }

  // populate distance array from the beginning to avoid at the current day
  for(int i = 0; i < SIZE_PING_AVG - 1; i++) {
    distanceArray[i] = AVG_DISTANCE_TRIGGER * 2;
  }

  // TCP and cloud functions
  Particle.function("command", executeCommand);
  server.begin();

  // set current day once the connection to WiFi and the cloud has been established
  waitUntil(WiFi.ready);
  waitUntil(Particle.connected);

  // set stepper speed and set initial postion
  Time.zone(2); //time zone difference (+2)
  currentDayNumber = Time.day();
  goToTime(Time.hourFormat12(), Time.minute());
  timeStampTime = millis();

  // if shown, hide information from the beginning
  String message = "H" + String(MESSAGE_SEPARATOR);
  server.print(message);
}

void loop() {
  // execute command when data is available
  if(Serial.available() > 0) {
    executeCommand(Serial.readString());
  }

  // make sure not to do something is not position has been set
  if(getPosition() == -1) {
    Serial.println("No start position is set!");
    delay(2000);
    return;
  }

  // set client when available
  if(!client.connected()) {
    // use built in LED to indicate that no client is connected
    digitalWrite(D7, LOW);
    client = server.available();
  }

  // echo everything back to the client
  if(client.connected()) {
    if(digitalRead(D7) == LOW) digitalWrite(D7, HIGH);

    while(client.available()) {
      String data = client.readStringUntil(MESSAGE_SEPARATOR);
      if(data == "ping") {
        String response = "P" + String(MESSAGE_SEPARATOR);
        server.print(response);
      }
    }
  }

  // if clock has been idle for more than THRESHOLD_IDLE seconds
  if(timeStampIdle != -1 && (millis() - timeStampIdle) > (THRESHOLD_IDLE * 1000) && getAvgDistance() > AVG_DISTANCE_TRIGGER) {
    String message = "H" + String(MESSAGE_SEPARATOR);
    server.print(message);
    timeStampIdle = -1;
    goToTime(Time.hourFormat12(), Time.minute());
  }

  // update time and day every sixty seconds
  if(timeStampIdle == -1 && (millis() - timeStampTime) > (TIME_UPDATE_INTERVAL * 1000)) {
    timeStampTime = millis();
    currentDayNumber = Time.day();
    goToTime(Time.hourFormat12(), Time.minute());
  }

  if(timeStampIdle == -1 && getAvgDistance() < AVG_DISTANCE_TRIGGER) {
    timeStampIdle = millis();
    goToDayNumber(currentDayNumber);
  }

  // when forward button is being pushed
  if(digitalRead(BUTTON_FORWARD) == HIGH) {
    int stepsTaken = 0;
    int newPosition;
    int dayShowing;

    while(digitalRead(BUTTON_FORWARD) == HIGH) {
      stepper.step(FORWARD_STEP);
      stepsTaken++;
      newPosition = (getPosition() + stepsTaken) % ONE_REVOLUTION;

      int newDay = getDayNumberFromPosition(newPosition);

      if(newDay != dayShowing) {
        dayShowing = newDay;
        String message = "D" + String(dayShowing) + MESSAGE_SEPARATOR;
        server.print(message);
      }
    }

    setPosition(newPosition);
    timeStampIdle = millis();

    Particle.publish("button", "forward");
  }

  // when back button is being pushed
  if(digitalRead(BUTTON_BACK) == HIGH) {
    int stepsTaken = 0;
    int newPosition;
    int dayShowing;

    while(digitalRead(BUTTON_BACK) == HIGH) {
      stepper.step(BACKWARD_STEP);
      stepsTaken++;

      int difference = getPosition() - stepsTaken;
      int newDay;

      if(difference >= 0) {
        newPosition = difference;
      } else {
        newPosition = ONE_REVOLUTION - ((stepsTaken - getPosition()) % ONE_REVOLUTION);
      }

      newDay = getDayNumberFromPosition(newPosition);

      if(newDay != dayShowing) {
        dayShowing = newDay;
        String message = "D" + String(dayShowing) + MESSAGE_SEPARATOR;
        server.print(message);
      }

    }

    setPosition(newPosition);
    timeStampIdle = millis();

    Particle.publish("button", "backward");
  }

  //add new ping sensor value and delay photon to save power
  addNumberToDistanceArray(getCurrentDistance());
  delay(50);
}

/********** HELPER METHODS **********/
void setPosition(short value) {
  if(value < 0 || value > ONE_REVOLUTION) {
    Serial.println("Position out of bounds! Position: " + String(value));
    return;
  }

  EEPROM.put(EERPROM_ADDRESS, value);
  Serial.println("New position is set to: " + String(value));
}

short getPosition() {
  short position;
  EEPROM.get(EERPROM_ADDRESS, position);

  if(position == 0xFFFF) position = -1;

  return position;
}

void goToPosition(int newPosition) {
  stepper.step(getPosition() - newPosition);
  setPosition(newPosition);
}

int getDayNumberFromPosition(int stepPosition) {
  int day = -1;

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

  if(dayNumber < 0 || dayNumber > 30) {
    Serial.println("Cannot go to day – day is not in interval!");
    return;
  }

  // send day to show graphic overlay
  server.print("D" + String(dayNumber) + MESSAGE_SEPARATOR);
  Particle.publish("showDay", dayNumber);
  Serial.println("Current day: " + String(dayNumber));

  if(dayNumber == 1) {
    int newPosition = offset;
    goToPosition(newPosition);
  } else {
    int newPosition = dayArray[dayNumber - 1] - offset;
    goToPosition(newPosition);
  }
}

void goToTime(int hour, int minute) {
  if(hour < 1 || hour > 12 || minute < 0 || minute > 59) {
    Serial.println("Time cannot be set to " + String(hour) + ":" + String(minute));
    return;
  }

  double totalNumberOfMinutes = hour == 12 ? minute : ((60 * hour) + minute);
  double stepSize = ((double) ONE_REVOLUTION) / ((double) NUMBER_OF_MINUTES_12_HOURS);
  int newPosition = totalNumberOfMinutes * stepSize;
  goToPosition(newPosition);

  Serial.println("Time set to " + String(hour) + ":" + String(minute));
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
      EEPROM.clear(); // clear to make sure that a new start position has to be set
      stepper.step(-1 * value); //negative is forward and positive backward
      break;
    case 'C':
      EEPROM.clear();
      break;
    case 'D':
      goToDayNumber(value);
      break;
    case 'T':
      goToTime(command.substring(1, 3).toInt(), command.substring(4).toInt());
      break;
    case 'N':
      Serial.print("IP: ");
      Serial.println(String(WiFi.localIP()) + ":" + String(PORT));
      Serial.print("SSID: ");
      Serial.println(WiFi.SSID());
      break;
    default:
      res = -1;
      break;
  }

  return res;
}

long getCurrentDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(5);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);

  long duration = pulseIn(ECHO, HIGH);
  long cm = (duration/2) / 29.1;

  return cm;
}

long getAvgDistance() {
  long sum = 0;

  for(int i = 0; i < SIZE_PING_AVG; i++) {
    sum += distanceArray[i];
  }

  return (sum / SIZE_PING_AVG);
}

void addNumberToDistanceArray(long newNumber) {
  // discard all values which are out of the sensor's ordinary range
  if (newNumber > 400 || newNumber < 2) return;

  for(int i = 0; i < SIZE_PING_AVG - 1; i++) {
    distanceArray[i] = distanceArray[i + 1];
  }

  distanceArray[SIZE_PING_AVG - 1] = newNumber;
}
