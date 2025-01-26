/*
Arduino Nano firmware using Alnitak protocol

Code adapted from https://github.com/jwellman80/ArduinoLightbox/blob/master/LEDLightBoxAlnitak.ino

The host (INDI server) sends commands starting with >, this firmware responds with *

Send     : >P000\n      // ping
Recieve  : *Pid000\n    // confirm

Send     : >S000\n      // request state
Recieve  : *Sid000\n    // returned state

Send     : >O000\n      // open shutter
Recieve  : *Oid000\n    // confirm

Send     : >C000\n      // close shutter
Recieve  : *Cid000\n    // confirm

Send     : >L000\n      // turn light on (uses set brightness value)
Recieve  : *Lid000\n    // confirm

Send     : >D000\n      // turn light off (brightness value should not be changed)
Recieve  : *Did000\n    // confirm

Send     : >Bxxx\n      // set brightness to xxx
Recieve  : *Bidxxx\n    // confirm

Send     : >Zxxx\n		// set closed angle to xxx
Recieve	 : *Zidxxx\n	// confirm

Send     : >Axxx\n		// set open angle to xxx
Recieve	 : *Aidxxx\n	// confirm

Send     : >J000\n      // get brightness
Recieve  : *Bidxxx\n    // returned brightness

Send     : >Hxxx\n		// get closed angle
Recieve	 : *Hidxxx\n	// returned angle

Send     : >Kxxx\n		// get open angle
Recieve	 : *Kidxxx\n	// returned angle

Send     : >Vxxx\n		// get firmware version
Recieve	 : *Vidxxx\n	// returned firmware verison
*/

#include <Servo.h>
#include <EEPROM.h>

#define MIN_SERVO_DELAY 200
#define SERVO_SPEED 0.5				// in degrees per ms
#define SERVO_DELAY_OFFSET 100		// delay added to delay() in ms

#define LED_PIN 5					// best to use a pin with a higher PWM frequency, so 5 and 6 for uno/nano
#define SERVO_PIN 9

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

uint8_t deviceId = 99;				// id for gastro flatcap
uint8_t motorStatus = STOPPED;
uint8_t lightStatus = OFF;
uint8_t coverStatus = CLOSED;
uint8_t brightness = 0;
uint16_t closedAngle = 0;
uint16_t openAngle = 0;

void setup() {
	servo.attach(SERVO_PIN);
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    analogWrite(LED_PIN, 0);
	closedAngle = readInt16EEPROM(CLOSED_ANGLE_ADDRESS);
	openAngle = readInt16EEPROM(OPEN_ANGLE_ADDRESS);
	brightness = EEPROM.read(BRIGHTNESS_ADDRESS);
	//TCCR0B = (TCCR0B & 0b11111000) | 0x02;		// set Timer 0 prescaler to 8
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
        char* dat;
        char temp[7];
        char buffer[20];
        memset(buffer, 0, 20);
        Serial.readBytesUntil('\n', buffer, 20);
		char* command = buffer;
		int i = 0;
		while(*command != '>') {
			command = buffer + i;
			i++;
		}
    	cmd = command + 1;
    	dat = command + 2;
		char data[3] = {0};
		strncpy(data, dat, 3);

        switch(*cmd) {
    	    /*
    	    Ping device
    	    Request: >P000\n
    	    Return : *Pid000\n
    	    */
            case 'P': {
    	        sprintf(temp, "*P%d000", deviceId);
    	        Serial.println(temp);
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
                Serial.println(temp);
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
        	    Serial.println(temp);
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
        	    Serial.println(temp);
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
        	    Serial.println(temp);
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
        	    Serial.println(temp);
				break;
            }
    	    /*
        	Set brightness
        	Request: >Bxxx\n
        	xxx = brightness value from 001-255
        	Return : *Bidxxx\n
        	xxx = value that brightness was set from 001-255
    	    */
            case 'B': {
        	    brightness = atoi(data);
				EEPROM.update(BRIGHTNESS_ADDRESS, brightness % 256);
        	    if(lightStatus == ON && coverStatus == CLOSED) {
        	    	analogWrite(LED_PIN, brightness % 256);
                }
        	    sprintf(temp, "*B%d%03d", deviceId, brightness % 256);
                Serial.println(temp);
				break;
            }
			/*
        	Set shutter closed angle
        	Request: >Zxxx\n
        	xxx = angle from 000-300
        	Return : *Zidxxx\n
        	xxx = value that closed angle was set from 000-360
    	    */
            case 'Z': {
        	    closedAngle = atoi(data);
				updateInt16EEPROM(CLOSED_ANGLE_ADDRESS, closedAngle % 1000);
        	    if(coverStatus == CLOSED) {
					moveServo(closedAngle % 1000);
                }
        	    sprintf(temp, "*Z%d%03d", deviceId, closedAngle % 1000);
                Serial.println(temp);
				break;
            }
			/*
        	Set shutter open angle
        	Request: >Axxx\n
        	xxx = angle from 000-300
        	Return : *Aidxxx\n
        	xxx = value that open angle was set from 000-360
    	    */
            case 'A': {
        	    openAngle = atoi(data);
				updateInt16EEPROM(OPEN_ANGLE_ADDRESS, openAngle % 1000);
        	    if(coverStatus == OPEN) {
					moveServo(openAngle % 1000);
                }
        	    sprintf(temp, "*A%d%03d", deviceId, openAngle % 1000);
                Serial.println(temp);
				break;
            }
			/*
        	Get brightness
        	Request: >J000\n
        	Return : *Jidxxx\n
        	xxx = current brightness value from 000-255
    	    */
            case 'J': {
				EEPROM.update(BRIGHTNESS_ADDRESS, brightness);
                sprintf(temp, "*J%d%03d", deviceId, brightness % 256);
                Serial.println(temp);
				break;
            }
			/*
        	Get shutter closed angle
        	Request: >K000\n
        	Return : *Kidxxx\n
        	xxx = value that closed angle was set from 000-360
    	    */
            case 'K': {
				closedAngle = readInt16EEPROM(CLOSED_ANGLE_ADDRESS);
                sprintf(temp, "*K%d%03d", deviceId, closedAngle % 1000);
                Serial.println(temp);
				break;
            }
			/*
        	Get shutter open angle
        	Request: >H000\n
        	Return : *Hidxxx\n
        	xxx = current brightness value from 000-360
    	    */
            case 'H': {
				openAngle = readInt16EEPROM(OPEN_ANGLE_ADDRESS);
                sprintf(temp, "*H%d%03d", deviceId, openAngle % 1000);
                Serial.println(temp);
				break;
            }
    	    /*
        	Get firmware version
        	Request: >V000\n
        	Return : *Vid001\n
    	    */
            case 'V': {
                sprintf(temp, "*V%d001", deviceId);
                Serial.println(temp);
				break;
            }
        }
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
		moveServo(openAngle);
		coverStatus = OPEN;
	} else {
		moveServo(closedAngle);
		coverStatus = CLOSED;
	}
}