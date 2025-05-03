#include "mbed.h"

#define CODE_LENGTH 4

char correctCode[CODE_LENGTH] = {'1', '3', '5', '9'};
char enterCode[CODE_LENGTH];
int enterDigits = 0;
int eventsIndex = 0;

AnalogIn potentiometer(A0);
AnalogIn lm35(A1);
AnalogIn mq2(A3);
DigitalOut alarmLed(LED1);
DigitalOut incorrectCodeLed(LED3);
PwmOut buzzer(D9);
UnbufferedSerial uartUsb(USBTX, USBRX, 115200);

void uartTask();
void availableCommands();
void systemStateUpdate(bool lastState, bool currentState, const char* elementName);

float lm35Reading = 0.0;
float lm35TempC = 0.0;

float analogReadingScaledWithTheLM35Formula(float analogReading);

float potentiometerReading = 0.00f;

bool overTempDetect = false;
bool overTempDetectState = false;
bool gasDetectState = false;

DigitalOut keypadRow[4] = {PB_3, PB_5, PC_7, PA_15};
DigitalIn keypadCol[4] = {PB_12, PB_13, PB_15, PC_6};

char matrixKeypadIndexToCharArray[] = {
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'
};

typedef struct systemEvent {
    time_t seconds;
    char typeOfEvent[14];
} systemEvent_t;

systemEvent_t arrayOfStoredEvents[100];

void inputsInit() {
    for(int i = 0; i < 4; i++) {
        keypadCol[i].mode(PullUp);
    }
}

void outputsInit() {
    alarmLed = 0;
    incorrectCodeLed = 0;
    buzzer = 0;
}

char matrixKeypadScan() {
    for(int row = 0; row < 4; row++) {
        for(int i = 0; i < 4; i++) {
            keypadRow[i] = 1;
        }
        keypadRow[row] = 0;

        for(int col = 0; col < 4; col++) {
            if(keypadCol[col] == 0) {
                return matrixKeypadIndexToCharArray[row * 4 + col];
            }
        }
    }
    return '\0';
}

char matrixKeypadUpdate() {
    static char lastKey = '\0';
    static Timer debounceTimer;
    static bool debounceTimerStarted = false;
    char currentKey = matrixKeypadScan();

    if(currentKey != '\0' && lastKey == '\0' && !debounceTimerStarted) {
        debounceTimer.reset();
        debounceTimer.start();
        debounceTimerStarted = true;
    }

    if(debounceTimerStarted && debounceTimer.elapsed_time().count() > 20000) {
        debounceTimer.stop();
        debounceTimerStarted = false;
        lastKey = currentKey;
        if(currentKey != '\0') {
            return currentKey;
        }
    }

    if(currentKey == '\0') {
        lastKey = '\0';
    }

    return '\0';
}

int main() {
    bool alarmState = false;

    inputsInit();
    outputsInit();

    uartUsb.write("\nSystem is on. Enter code when alarm is activated to deactivate it.\r\n", 69);
    uartUsb.write("\nPress 'q' to list available commands.\r\n", 40);

    bool alarmActivated = false;

    while (true) {

        uartTask();

        float potentiometerReading = potentiometer.read();

        lm35Reading = lm35.read();
        lm35TempC = analogReadingScaledWithTheLM35Formula(lm35Reading);

        if (potentiometer.read() >=0.51f && !alarmActivated) {
            alarmState = alarmActivated;
            alarmActivated = true;
            uartUsb.write("\nAlarm activated - potentiometer value above 0.50\r\n", 52);
            uartUsb.write("\nEnter code to deactivate alarm:\r\n", 34);
            alarmLed = 1;
            systemStateUpdate(alarmState, alarmActivated, "POT. ALARM");
        }

        if (lm35TempC > 32.0 && !alarmActivated) {
            alarmState = alarmActivated;
            alarmActivated = true;
            uartUsb.write("\nAlarm activated - temperature value above 32.0\r\n", 49);
            uartUsb.write("\nEnter code to deactivate alarm:\r\n", 34);
            alarmLed = 1;
            systemStateUpdate(alarmState, alarmActivated, "TEMP. ALARM");
        }

        if (!mq2 && !alarmActivated) {
            alarmState = alarmActivated;
            alarmActivated = true;
            uartUsb.write("\nAlarm activated - gas is being detected\r\n", 43);
            uartUsb.write("\nEnter code to deactivate alarm:\r\n", 34);
            alarmLed = 1;
            systemStateUpdate(alarmState, alarmActivated, "GAS ALARM");
        }

        char key = matrixKeypadUpdate();

        if (key != '\0') {
            if(key >= '0' && key <= '9') {
                if(enterDigits < CODE_LENGTH) {
                enterCode[enterDigits++] = key;
                uartUsb.write(&key, 1);
                }
            } else if(key == '#') {
                if(enterDigits == CODE_LENGTH) {
                    //uartUsb.write("\r\nCode entered: ", 16);
                    //uartUsb.write(enterCode, CODE_LENGTH);
                    //uartUsb.write("\r\n", 2);
                    bool correct = true;
                    for(int i = 0; i < CODE_LENGTH; i++) {
                        if(enterCode[i] != correctCode[i]) {
                            correct = false;
                            break;
                        }
                    }

                    if (correct && alarmActivated) {
                        alarmActivated = false;
                        alarmLed = 0;
                        buzzer = 0;
                        uartUsb.write("\r\nAlarm deactivated\r\n", 22);
                        uartUsb.write("Date and time of incident recorded.\r\n", 38);
                        systemStateUpdate(alarmState, alarmActivated, "POT. ALARM");
                        uartTask();
                    } else {
                        uartUsb.write("\r\nIncorrect code\r\n", 18);
                        incorrectCodeLed = 1;
                    }
                } else {
                    uartUsb.write("\r\nPlease enter 4 digits\r\n", 25);
                }
                enterDigits = 0;
            } else if (key == '*') {
                        if (eventsIndex == 0) {
            uartUsb.write("There are no events stored.\r\n", 29);
            } else {
                for (int i = 0; i < eventsIndex; i++) {
                char str[100];
                sprintf(str, "Event = %s\r\n", arrayOfStoredEvents[i].typeOfEvent);
                uartUsb.write(str, strlen(str));
                sprintf(str, "Date and Time = %s\r\n", ctime(&arrayOfStoredEvents[i].seconds));
                uartUsb.write(str, strlen(str));
                uartUsb.write("\r\n", 2);
                }
              }
            }
        }

        if (alarmActivated) {
        buzzer.period(1.0/100);
        buzzer = 10; 
        ThisThread::sleep_for(200ms);
        buzzer.period(1.0/105);
        buzzer = 10;
        ThisThread::sleep_for(200ms);
        buzzer = 0;
        }
    
        ThisThread::sleep_for(1ms);
    }
}

void uartTask() {
    char receivedChar = '\0';
    char str[100];
    int stringLength;
    if(uartUsb.readable()) {
        uartUsb.read(&receivedChar, 1);
        switch (receivedChar) {
            
        case 's':
        case 'S':
            struct tm rtcTime;
            int strIndex;
                    
            uartUsb.write("\r\nType four digits for the current year (YYYY): ", 48);
            for( strIndex=0; strIndex<4; strIndex++ ) {
                uartUsb.read(&str[strIndex] , 1);
                uartUsb.write(&str[strIndex] ,1);
            }
            str[4] = '\0';
            rtcTime.tm_year = atoi(str) - 1900;
            uartUsb.write("\r\n", 2);

            uartUsb.write("\r\nType two digits for the current month (01-12): ", 49);
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read(&str[strIndex] , 1);
                uartUsb.write(&str[strIndex] ,1);
            }
            str[2] = '\0';
            rtcTime.tm_mon  = atoi(str) - 1;
            uartUsb.write("\r\n", 2);

            uartUsb.write("\r\nType two digits for the current day (01-31): ", 47);
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read(&str[strIndex] , 1);
                uartUsb.write(&str[strIndex] ,1);
            }
            str[2] = '\0';
            rtcTime.tm_mday = atoi(str);
            uartUsb.write("\r\n", 2);

            uartUsb.write("\r\nType two digits for the current hour (00-23): ", 48);
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read(&str[strIndex] , 1);
                uartUsb.write(&str[strIndex] ,1);
            }
            str[2] = '\0';
            rtcTime.tm_hour = atoi(str);
            uartUsb.write("\r\n", 2);

            uartUsb.write("\r\nType two digits for the current minutes (00-59): ", 51);
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read(&str[strIndex] , 1);
                uartUsb.write(&str[strIndex] ,1);
            }
            str[2] = '\0';
            rtcTime.tm_min  = atoi(str);
            uartUsb.write("\r\n", 2);

            uartUsb.write("\r\nType two digits for the current seconds (00-59): ", 51);
            for( strIndex=0; strIndex<2; strIndex++ ) {
                uartUsb.read(&str[strIndex] , 1);
                uartUsb.write(&str[strIndex] ,1);
            }
            str[2] = '\0';
            rtcTime.tm_sec  = atoi(str);
            uartUsb.write("\r\n", 2);

            rtcTime.tm_isdst = -1;
            set_time(mktime(&rtcTime));
            uartUsb.write("\r\nDate and time has been set.\r\n", 31);

            break;
                        
            case 't':
            case 'T':
                time_t epochSeconds;
                epochSeconds = time(NULL);
                sprintf ( str, "Date and Time = %s", ctime(&epochSeconds));
                uartUsb.write( str , strlen(str) );
                uartUsb.write( "\r\n", 2 );
                break;

        default:
            availableCommands();
            break;

        }
    }
}

void availableCommands()
{
    uartUsb.write("Available commands:\r\n", 21);
    uartUsb.write("Press 's' or 'S' to set the date and time\r\n", 43);
    uartUsb.write("Press 't' or 'T' to get current date and time\r\n", 47);
    uartUsb.write("Press '*' on the keypad to display stored alarm events\r\n\r\n", 58);
}

void systemStateUpdate(bool lastState, bool currentState, const char* elementName) {
    char eventAndStateStr[14] = "";

    if (lastState != currentState) {

        strcat(eventAndStateStr, elementName);
        if (currentState) {
            strcat(eventAndStateStr, "_ON");
        } else {
            strcat(eventAndStateStr, "_OFF");
        }

        arrayOfStoredEvents[eventsIndex].seconds = time(NULL);
        strcpy(arrayOfStoredEvents[eventsIndex].typeOfEvent,eventAndStateStr);
        if (eventsIndex < 100 - 1) {
            eventsIndex++;
        } else {
            eventsIndex = 0;
        }

        uartUsb.write(eventAndStateStr , strlen(eventAndStateStr));
        uartUsb.write("\r\n", 2);
    }
}

float analogReadingScaledWithTheLM35Formula(float analogReading)
{
    return analogReading * 330.0;
}