/*
Arduino Nano firmware using Alnitak protocol

Code adapted from https://github.com/jwellman80/ArduinoLightbox/blob/master/LEDLightBoxAlnitak.ino


Send     : >S000\n      //request state
Recieve  : *Sid000\n    //returned state

Send     : >Bxxx\n      //set brightness to xxx
Recieve  : *Bidxxx\n    //confirming brightness set to xxx

Send     : >J000\n      //get brightness
Recieve  : *Bidxxx\n    //brightness value of xxx (assuming as set from above)

Send     : >L000\n      //turn light on (uses set brightness value)
Recieve  : *Lid000\n    //confirms light turned on

Send     : >D000\n      //turn light off (brightness value should not be changed)
Recieve  : *Did000\n    //confirms light turned off.


Added open/closed cover angle commands:

Send     : >Zxxx\n		//set closed angle to xxx
Recieve	 : *Zidxxx\n	//confirming angle set to xxx

Send     : >Axxx\n		//set open angle to xxx
Recieve	 : *Aidxxx\n	//confirming angle set to xxx
*/

#include <Servo.h>
#include <EEPROM.h>

#define MIN_SERVO_DELAY 200
#define SERVO_SPEED 0.5				// in degrees per ms
#define SERVO_DELAY_OFFSET 100		// delay added to delay() in ms

#define LED_PIN 5

Servo servo;

enum motorStatuses {
	STOPPED,
	RUNNING
};

enum lightStatuses {
	OFF,
	ON
};

enum shutterStatuses {
	UNKNOWN,
	CLOSED,
	OPEN
};

/*
! IMPORTANT:
Both CLOSED_ANGLE_ADDRESS and OPEN_ANGLE_ADDRESS store 16 bit integers (255 just isn't enough for the positions).
That means they use two EEPROM addresses each (CLOSED_ANGLE uses addresses 1 and 2, for example)
*/
enum addresses {
	BRIGHTNESS_ADDRESS = 0,
	CLOSED_ANGLE_ADDRESS = 1,
	OPEN_ANGLE_ADDRESS = 3
};

uint8_t deviceId = 99;			// id for gastro flatcap
uint8_t motorStatus = STOPPED;
uint8_t lightStatus = OFF;
uint8_t coverStatus = CLOSED;
uint8_t brightness = 0;
uint16_t closedAngle = 0;
uint16_t openAngle = 0;

void setup() {
	servo.attach(9);
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    analogWrite(LED_PIN, 0);
	closedAngle = readInt16EEPROM(CLOSED_ANGLE_ADDRESS);
	openAngle = readInt16EEPROM(OPEN_ANGLE_ADDRESS);
	brightness = EEPROM.read(BRIGHTNESS_ADDRESS);
}

void loop() {
    handleSerial();
}

void moveServo(int angle) {
	servo.write(angle);
	motorStatus = RUNNING;
	delay(max(MIN_SERVO_DELAY, SERVO_DELAY_OFFSET + round(abs(servo.read() - angle) / SERVO_SPEED)));
	motorStatus = STOPPED;
}

void updateInt16EEPROM(int address, uint16_t value) {
	EEPROM.update(address, value);
	EEPROM.update(address + 1, value >> 8);
}
uint16_t readInt16EEPROM(int address) {
	return (EEPROM.read(address + 1) << 8) | EEPROM.read(address);
}

void handleSerial() {
    if(Serial.available() >= 6) {
        char* cmd;
        char* data;
        char temp[7];
        char buffer[20];
        memset(buffer, '!', 20);
        Serial.readBytesUntil('\n', buffer, 20);
		char* command = buffer;
		int i = 0;
		while(*command != ">") {
			command = buffer + i;
			i++;
		}
    	cmd = command + 1;
    	data = command + 2;

        switch(*cmd) {
    	    /*
    	    Ping device
    	    Request: >P000\n
    	    Return : *Pid000\n
    	    */
            case 'P': {
    	        sprintf(temp, "*P%d000", deviceId);
    	        Serial.print(temp);
    	        break;
            }
            /*
        	Open shutter
        	Request: >O000\n
        	Return : *Oid000\n
    	    */
            case 'O': {
        	    sprintf(temp, "*O%d000", deviceId);
        	    setShutter(OPEN);
        	    break;
            }
            /*
        	Close shutter
        	Request: >C000\n
        	Return : *Cid000\n
    	    */
            case 'C': {
        	    sprintf(temp, "*C%d000", deviceId);
        	    setShutter(CLOSED);
        	    break;
            }
    	    /*
        	Turn light on
        	Request: >L000\n
        	Return : *Lid000\n
    	    */
            case 'L': {
				if(coverStatus == CLOSED) {
        	    	analogWrite(LED_PIN, brightness);
					lightStatus = ON;
				}
        	    sprintf(temp, "*L%d000", deviceId);
        	    break;
            }
    	    /*
        	Turn light off
        	Request: >D000\n
        	Return : *Did000\n
    	    */
            case 'D': {
				analogWrite(LED_PIN, 0);
				lightStatus = OFF;
        	    sprintf(temp, "*D%d000", deviceId);
        	    break;
            }
    	    /*
        	Set brightness
        	Request: >Bxxx\n
        	xxx = brightness value from 000-255
        	Return : *Bidyyy\n
        	yyy = value that brightness was set from 001-255
    	    */
            case 'B': {
        	    brightness = atoi(data);
				if(brightness < 1 || brightness > 255) {
					break;
				}
				EEPROM.update(BRIGHTNESS_ADDRESS, brightness);
        	    if(lightStatus == ON && coverStatus == CLOSED) {
        	    	analogWrite(LED_PIN, brightness);
                }
        	    sprintf(temp, "*B%d%03d", deviceId, brightness);
                break;
            }
			/*
        	Set shutter closed angle
        	Request: >Zxxx\n
        	xxx = angle from 000-300
        	Return : *Zidyyy\n
        	yyy = value that closed angle was set from 000-300
    	    */
            case 'Z': {
        	    closedAngle = atoi(data);
				updateInt16EEPROM(CLOSED_ANGLE_ADDRESS, closedAngle);
        	    if(coverStatus == CLOSED) {
					moveServo(closedAngle);
                }
        	    sprintf(temp, "*Z%d%03d", deviceId, closedAngle);
                break;
            }
			/*
        	Set shutter open angle
        	Request: >Axxx\n
        	xxx = angle from 000-300
        	Return : *Aidyyy\n
        	yyy = value that open angle was set from 000-300
    	    */
            case 'A': {
        	    openAngle = atoi(data);
				updateInt16EEPROM(OPEN_ANGLE_ADDRESS, openAngle);
        	    if(coverStatus == OPEN) {
					moveServo(openAngle);
                }
        	    sprintf(temp, "*A%d%03d", deviceId, openAngle);
                break;
            }
			/*
        	Get shutter closed angle
        	Request: >K000\n
        	Return : *Kidyyy\n
        	yyy = value that closed angle was set from 000-300
    	    */
            case 'K': {
				EEPROM.update(CLOSED_ANGLE_ADDRESS, closedAngle);
                sprintf(temp, "*K%d%03d", deviceId, closedAngle);
                break;
            }
			/*
        	Get shutter open angle
        	Request: >H000\n
        	Return : *Hidyyy\n
        	yyy = current brightness value from 000-255
    	    */
            case 'H': {
				EEPROM.update(OPEN_ANGLE_ADDRESS, openAngle);
                sprintf(temp, "*H%d%03d", deviceId, openAngle);
                break;
            }
    	    /*
        	Get brightness
        	Request: >J000\n
        	Return : *Jidyyy\n
        	yyy = current brightness value from 000-255
    	    */
            case 'J': {
				EEPROM.update(BRIGHTNESS_ADDRESS, brightness);
                sprintf(temp, "*J%d%03d", deviceId, brightness);
                break;
            }
    	    /*
        	Get device status:
        	Request: >S000\n
        	Return : *SidMLC\n
        	M  = motor status (0 stopped, 1 running)
        	L  = light status (0 off, 1 on)
        	C  = cover status (0 moving, 1 closed, 2 open)
    	    */
            case 'S': {
                sprintf(temp, "*S%d%d%d%d", deviceId, motorStatus, lightStatus, coverStatus);
                break;
            }
    	    /*
        	Get firmware version
        	Request: >V000\n
        	Return : *Vid001\n
    	    */
            case 'V': {
                sprintf(temp, "*V%d001", deviceId);
                break;
            }
        }
		Serial.println(temp);
    	while(Serial.available() > 0) {
    		Serial.read();  
        }
    }
}

void setShutter(int shutter) {
	if(shutter == UNKNOWN) {
		return;
	}
	if(shutter == OPEN) {
		analogWrite(LED_PIN, 0);
		lightStatus = OFF;
		coverStatus = OPEN;
		moveServo(openAngle);
	} else {
		coverStatus = CLOSED;
		moveServo(closedAngle);
	}
}