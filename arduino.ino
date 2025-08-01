/*
Arduino Nano firmware using Alnitak protocol

Code adapted from https://github.com/jwellman80/ArduinoLightbox/blob/master/LEDLightBoxAlnitak.ino

The host (INDI server) sends commands starting with >, this firmware responds with *

Send     : >P000\n      // ping
Recieve  : *Pid000\n    // confirm

Send     : >S000\n      // request state
Recieve  : *Sid000\n    // returned state

Send     : >O000\n      // unpark shutter
Recieve  : *Oid000\n    // confirm

Send     : >C000\n      // park shutter
Recieve  : *Cid000\n    // confirm

Send     : >L000\n      // turn light on (uses set brightness value)
Recieve  : *Lid000\n    // confirm

Send     : >D000\n      // turn light off (brightness value should not be changed)
Recieve  : *Did000\n    // confirm

Send     : >Bxxx\n      // set brightness to xxx
Recieve  : *Bidxxx\n    // confirm

Send     : >Zxxx\n		// set parked angle to xxx
Recieve	 : *Zidxxx\n	// confirm

Send     : >Axxx\n		// set unpark angle to xxx
Recieve	 : *Aidxxx\n	// confirm

Send     : >J000\n      // get brightness
Recieve  : *Bidxxx\n    // returned brightness

Send     : >Hxxx\n		// get park angle
Recieve	 : *Hidxxx\n	// returned angle

Send     : >Kxxx\n		// get unpark angle
Recieve	 : *Kidxxx\n	// returned angle

Send     : >Vxxx\n		// get firmware version
Recieve	 : *Vidxxx\n	// returned firmware verison
*/

#include <Servo.h>
#include <EEPROM.h>

#define SERVO_INCREMENT 1			// in degrees
#define SERVO_DELAY	20				// delay in ms after each servo increment, speed of the servo can be calculated by
									// SERVO_INCREMENT / SERVO_DELAY, the result is in deg/ms

#define LED_PIN 5					// best to use a pin with a higher PWM frequency, so 5 or 6 for uno/nano
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
	PARKED,
	UNPARKED
};

/*
! IMPORTANT:
Both PARK_ANGLE_ADDRESS and UNPARK_ANGLE_ADDRESS store 16 bit integers (255 just isn't enough for the positions).
That means they use two EEPROM addresses each (parkAngle uses addresses 1 and 2, for example)
*/
enum addresses {
	BRIGHTNESS_ADDRESS = 0,
	PARK_ANGLE_ADDRESS = 1,
	UNPARK_ANGLE_ADDRESS = 3,
	SHUTTER_STATUS_ADDRESS = 5
};

uint8_t deviceId = 99;				// id for gastro flatcap
uint8_t motorStatus = STOPPED;
uint8_t lightStatus = OFF;
uint8_t coverStatus = PARKED;
uint8_t brightness = 0;
uint16_t parkAngle = 0;
uint16_t unparkAngle = 0;
uint16_t servoPosition = 0;

void setup() {
	servo.attach(SERVO_PIN);
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    analogWrite(LED_PIN, 0);
	parkAngle = readInt16EEPROM(PARK_ANGLE_ADDRESS);
	unparkAngle = readInt16EEPROM(UNPARK_ANGLE_ADDRESS);
	brightness = EEPROM.read(BRIGHTNESS_ADDRESS);
	coverStatus = EEPROM.read(SHUTTER_STATUS_ADDRESS);
	servoPosition = (coverStatus == PARKED) ? parkAngle : unparkAngle;
    servo.write(servoPosition);
	while(Serial.available()) {
		Serial.read();			// clears buffer
	}
	//TCCR0B = (TCCR0B & 0b11111000) | 0x02;		// set Timer 0 prescaler to 8
}

void loop() {
    handleSerial();
}

void moveServo(int angle) {
	motorStatus = RUNNING;
	int direction = (servoPosition > angle) ? -1 : 1;
	while(abs(servoPosition - angle) >= SERVO_INCREMENT) {
		servoPosition += SERVO_INCREMENT * direction;
		servo.write(servoPosition);
		delay(SERVO_DELAY);
	}
	servo.write(angle);
	servoPosition = angle;
	delay(SERVO_DELAY);
	motorStatus = STOPPED;
}

void updateInt16EEPROM(int address, uint16_t value) {
	EEPROM.update(address, (uint8_t)(value >> 8));
	EEPROM.update(address + 1, (uint8_t)(value & 0xFF));
}
uint16_t readInt16EEPROM(int address) {
	return (EEPROM.read(address) << 8) | EEPROM.read(address + 1);
}

void handleSerial() {
    if(Serial.available() < 6) {
		return;
	}
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
            sprintf(temp, "*P%02d000", deviceId);
            Serial.println(temp);
			break;
        }
		/*
    	Get device status:
    	Request: >S000\n
    	Return : *SidMLC\n
    	M  = motor status (0 stopped, 1 running)
    	L  = light status (0 off, 1 on)
    	C  = cover status (0 moving, 1 parked, 2 unparked)
        */
        case 'S': {
            sprintf(temp, "*S%02d%01d%01d%01d", deviceId, motorStatus, lightStatus, coverStatus);
            Serial.println(temp);
			break;
        }
        /*
    	Unpark shutter
    	Request: >O000\n
    	Return : *Oid000\n
        */
        case 'O': {
    	    sprintf(temp, "*O%02d000", deviceId);
    	    setShutter(UNPARKED);
    	    Serial.println(temp);
			break;
        }
        /*
    	Park shutter
    	Request: >C000\n
    	Return : *Cid000\n
        */
        case 'C': {
    	    sprintf(temp, "*C%02d000", deviceId);
    	    setShutter(PARKED);
    	    Serial.println(temp);
			break;
        }
        /*
    	Turn light on
    	Request: >L000\n
    	Return : *Lid000\n
        */
        case 'L': {
			if(coverStatus == PARKED) {
    	    	analogWrite(LED_PIN, brightness);
				lightStatus = ON;
			}
    	    sprintf(temp, "*L%02d000", deviceId);
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
    	    sprintf(temp, "*D%02d000", deviceId);
    	    Serial.println(temp);
			break;
        }
        /*
    	Set brightness
    	Request: >Bxxx\n
    	xxx = brightness value from 000-255
    	Return : *Bidxxx\n
    	xxx = value that brightness was set from 000-255
        */
        case 'B': {
    	    brightness = atoi(data);
			EEPROM.update(BRIGHTNESS_ADDRESS, brightness % 256);
    	    if(lightStatus == ON && coverStatus == PARKED) {
    	    	analogWrite(LED_PIN, brightness % 256);
            }
    	    sprintf(temp, "*B%02d%03d", deviceId, brightness % 256);
            Serial.println(temp);
			break;
        }
		/*
    	Set shutter park angle
    	Request: >Zxxx\n
    	xxx = angle from 000-360
    	Return : *Zidxxx\n
    	xxx = value that park angle was set from 000-360
        */
        case 'Z': {
    	    parkAngle = atoi(data);
			updateInt16EEPROM(PARK_ANGLE_ADDRESS, parkAngle % 360);
    	    if(coverStatus == PARKED) {
				moveServo(parkAngle % 360);
            }
    	    sprintf(temp, "*Z%02d%03d", deviceId, parkAngle % 360);
            Serial.println(temp);
			break;
        }
		/*
    	Set shutter unpark angle
    	Request: >Axxx\n
    	xxx = angle from 000-360
    	Return : *Aidxxx\n
    	xxx = value that unpark angle was set from 000-360
        */
        case 'A': {
    	    unparkAngle = atoi(data);
			updateInt16EEPROM(UNPARK_ANGLE_ADDRESS, unparkAngle % 360);
    	    if(coverStatus == UNPARKED) {
				moveServo(unparkAngle % 360);
            }
    	    sprintf(temp, "*A%02d%03d", deviceId, unparkAngle % 360);
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
            sprintf(temp, "*J%02d%03d", deviceId, brightness % 256);
            Serial.println(temp);
			break;
        }
		/*
    	Get shutter park angle
    	Request: >K000\n
    	Return : *Kidxxx\n
    	xxx = value that park angle was set from 000-360
        */
        case 'K': {
			parkAngle = readInt16EEPROM(PARK_ANGLE_ADDRESS);
            sprintf(temp, "*K%02d%03d", deviceId, parkAngle % 360);
            Serial.println(temp);
			break;
        }
		/*
    	Get shutter unpark angle
    	Request: >H000\n
    	Return : *Hidxxx\n
    	xxx = value that unpark angle was set from 000-360
        */
        case 'H': {
			unparkAngle = readInt16EEPROM(UNPARK_ANGLE_ADDRESS);
            sprintf(temp, "*H%02d%03d", deviceId, unparkAngle % 360);
            Serial.println(temp);
			break;
        }
        /*
    	Get firmware version
    	Request: >V000\n
    	Return : *Vid001\n
        */
        case 'V': {
            sprintf(temp, "*V%02d001", deviceId);
            Serial.println(temp);
			break;
        }
    }
    while(Serial.available() > 0) {
    	Serial.read();  
    }
}

void setShutter(int shutter) {
	if(shutter == UNKNOWN) {
		return;
	}
	if(shutter == PARKED) {
		analogWrite(LED_PIN, 0);
		lightStatus = OFF;
		coverStatus = UNKNOWN;
		moveServo(parkAngle);
	} else if(shutter == UNPARKED) {
		coverStatus = UNKNOWN;
		moveServo(unparkAngle);
	}
	coverStatus = shutter;
	EEPROM.update(SHUTTER_STATUS_ADDRESS, shutter);
}