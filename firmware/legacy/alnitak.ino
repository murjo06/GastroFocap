/*
Arduino Nano firmware using Alnitak protocol

Code adapted from https://github.com/jwellman80/ArduinoLightbox/blob/master/LEDLightBoxAlnitak.ino


Send     : >S000\n      //request state
Recieve  : *S19000\n    //returned state

Send     : >B128\n      //set brightness 128
Recieve  : *B19128\n    //confirming brightness set to 128

Send     : >J000\n      //get brightness
Recieve  : *B19128\n    //brightness value of 128 (assuming as set from above)

Send     : >L000\n      //turn light on (uses set brightness value)
Recieve  : *L19000\n    //confirms light turned on

Send     : >D000\n      //turn light off (brightness value should not be changed)
Recieve  : *D19000\n    //confirms light turned off.
*/

#include <Servo.h>
#include <EEPROM.h>

#define OPEN_ANGLE = 30				// degrees for servo motor
#define CLOSED_ANGLE = 300

#define MIN_SERVO_DELAY 200
#define SERVO_SPEED 0.5				// in degrees per ms
#define SERVO_DELAY_OFFSET 100		// delay added to delay() in ms

#define LED_PIN = 5

#define BRIGHTNESS_ADDRESS 0

Servo servo;

enum motorStatuses {
	STOPPED = 0,
	RUNNING
};

enum lightStatuses {
	OFF = 0,
	ON
};

enum shutterStatuses {
	UNKNOWN = 0,
	CLOSED,
	OPEN
};

uint8_t deviceId = 99;
uint8_t motorStatus = STOPPED;
uint8_t lightStatus = OFF;
uint8_t coverStatus = CLOSED;
uint8_t brightness = 0;

void setup() {
	servo.attach(9);
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);
    analogWrite(LED_PIN, 0);
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

void handleSerial() {
    if(Serial.available() >= 6) {
        char* cmd;
        char* data;
        char temp[10];
        int len = 0;
        char str[20];
        memset(str, 0, 20);
        Serial.readBytesUntil('\n', str, 20);
    	cmd = str + 1;
    	data = str + 2;

        switch(*cmd) {
    	    /*
    	    Ping device
    	    Request: >P000\n
    	    Return : *Pid000\n
    	    id = deviceId
    	    */
            case 'P': {
    	        sprintf(temp, "*P%d000", deviceId);
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
        	    	lightStatus = ON;
        	    	analogWrite(LED_PIN, brightness);
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
        	    sprintf(temp, "*D%d000", deviceId);
        	    Serial.println(temp);
        	    lightStatus = OFF;
        	    analogWrite(LED_PIN, 0);
        	    break;
            }
    	    /*
        	Set brightness
        	Request: >Bxxx\n
        	xxx = brightness value from 000-255
        	Return : *Bidyyy\n
        	yyy = value that brightness was set from 000-255
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
        	    Serial.println(temp);
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
	if(shutter != OPEN || shutter != CLOSED) {
		return;
	}
	if(shutter == OPEN && coverStatus != OPEN) {
		analogWrite(LED_PIN, 0);
		lightStatus = OFF;
		coverStatus = OPEN;
		moveServo(OPEN_ANGLE);
	} else if(shutter == CLOSED && coverStatus != CLOSED) {
		coverStatus = CLOSED;
		moveServo(CLOSED_ANGLE);
	} else {
		coverStatus = shutter;
		moveServo((shutter == OPEN) ? OPEN_ANGLE : CLOSED_ANGLE);
	}
}