#include <s3servo.h>
#include <AccelStepper.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TMCStepper.h>
#include <Streaming.h>
#include <Ticker.h>

#define EXTERNAL_EEPROM
//#define USE_WC_EEPROM

#ifdef EXTERNAL_EEPROM
#include <Wire.h>
#else
#include <EEPROM.h>
#endif

#define BUFFER_SIZE 40

#define LED 2
#define SERVO 38

#define SDA 37
#define SCL 36
#define EEPROM_WC 35				// write control for M24C64, drive high to prevent writing, toggled by USE_WC_EEPROM
#define EEPROM_ADDRESS 0b1010000

#define EN 17						// enable
#define DIR 15						// direction
#define STEP 16						// step
#define TX 8						// receive pin 
#define RX 18						// transmit pin
#define DRIVER_ADDRESS 0b00 		// TMC2209 driver address according to MS1 and MS2
#define R_SENSE 0.1f				// my board uses R100 resistors because of the JLC parts library

#define RMS_CURRENT 600

#define TEMP 13

#define PERIOD_US 2000				// at most 1 / STEPPER_SPEED

#define STEPPER_SPEED 200
#define STEPPER_ACCELERATION 800

#define SERVO_INCREMENT 1			// in degrees
#define SERVO_DELAY	20				// delay in ms after each servo increment, speed of the servo can be calculated by
                                    // SERVO_INCREMENT / SERVO_DELAY, the result is in deg/ms

#define DISABLE_DELAY 15000

s3servo servo;

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

EEPROM storage is in little-endian, since this is what the ESP32 uses normally
*/
enum addresses {
	BRIGHTNESS_ADDRESS = 0,
	PARK_ANGLE_ADDRESS = 1,
	UNPARK_ANGLE_ADDRESS = 3,
	SHUTTER_STATUS_ADDRESS = 5,
    FOCUSER_POSITION_ADDRESS = 6            // takes 4 bytes
};

uint8_t deviceId = 99;				// id for gastro flatcap
uint8_t motorStatus = STOPPED;
uint8_t lightStatus = OFF;
uint8_t coverStatus = PARKED;
uint8_t brightness = 0;
uint16_t parkAngle = 0;
uint16_t unparkAngle = 0;
uint16_t servoPosition = 0;

AccelStepper stepper(AccelStepper::DRIVER, STEP, DIR);

TMC2209Stepper TMCdriver(&Serial2, R_SENSE, DRIVER_ADDRESS);

OneWire oneWire(TEMP);
DallasTemperature sensors(&oneWire);

float tCoeff = 0;

unsigned long currentPosition = 0;
unsigned long targetPosition = 0;
unsigned long lastSavedPosition = 0;
long millisLastMove = 0;
bool isEnabled = false;

Ticker stepperTicker;

void setup() {
    pinMode(LED, OUTPUT);
    pinMode(EN, OUTPUT);
	pinMode(STEP, OUTPUT);
	pinMode(DIR, OUTPUT);
	pinMode(SERVO, OUTPUT);
	
	Serial.begin(9600);
	Serial2.begin(115200, SERIAL_8N1, RX, TX);
	while(!Serial || !Serial2) {
		delay(5);
	}
	while(Serial.available()) {
		Serial.read();
	}
	#ifndef EXTERNAL_EEPROM
	EEPROM.begin(10);
	EEPROM.get(PARK_ANGLE_ADDRESS, parkAngle);
	EEPROM.get(UNPARK_ANGLE_ADDRESS, unparkAngle);
	brightness = EEPROM.read(BRIGHTNESS_ADDRESS);
	coverStatus = EEPROM.read(SHUTTER_STATUS_ADDRESS);
	EEPROM.get(FOCUSER_POSITION_ADDRESS, currentPosition);
	#else
	#ifdef USE_WC_EEPROM
	pinMode(EEPROM_WC, OUTPUT);
	digitalWrite(EEPROM_WC, HIGH);
	#endif
	Wire.begin(SDA, SCL);
	Wire.setClock(100000);
	parkAngle = (uint16_t)eepromReadLong(PARK_ANGLE_ADDRESS, 2) % 360;

	unparkAngle = (uint16_t)eepromReadLong(UNPARK_ANGLE_ADDRESS, 2) % 360;

	brightness = (uint8_t)eepromReadByte(BRIGHTNESS_ADDRESS) % 256;
	coverStatus = (uint8_t)eepromReadByte(SHUTTER_STATUS_ADDRESS);

	currentPosition = eepromReadLong(FOCUSER_POSITION_ADDRESS, 4);
	#endif
	servo.attach(SERVO, 0, 270);
	servoPosition = (coverStatus == PARKED) ? parkAngle : unparkAngle;

    ledcAttach(LED, 1000, 8);				// make sure that the MOSFET's gate charge is small enough for maximum pin current of 20 mA
	ledcWrite(LED, 0);

	TMCdriver.begin();
	TMCdriver.toff(3);						// enables driver in software
	TMCdriver.rms_current(RMS_CURRENT);
	TMCdriver.microsteps(0);

	TMCdriver.en_spreadCycle(false);
	TMCdriver.pwm_autoscale(true);			// needed for stealthChop
	TMCdriver.I_scale_analog(false);
	TMCdriver.pdn_disable(true);

	stepper.setMaxSpeed(STEPPER_SPEED);
	stepper.setAcceleration(STEPPER_ACCELERATION);
	stepper.setPinsInverted(false, false, true);		// dir, step, en
	stepper.setEnablePin(EN);
	stepper.disableOutputs();

	millisLastMove = millis();

	if(currentPosition < 0) {
		currentPosition = 0;
	}

	stepper.setCurrentPosition(currentPosition);
	lastSavedPosition = currentPosition;
	targetPosition = currentPosition;

	sensors.begin();
	if(sensors.getDeviceCount()) {
        delay(5);
    }

	stepperTicker.attach_ms(2, stepperRun);
}

void loop() {
    if(Serial.available() < 3) {
		return;
	}
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
	int i = 0;
	bool isFocuserCommand = false;
	while(Serial.available()) {
		if(i >= BUFFER_SIZE - 1) {
			break;
		}
		bool end = false;
		char temp = Serial.read();
		switch(temp) {
			case ':': {
				isFocuserCommand = true;
				break;
			}
			case '>': {
				isFocuserCommand = false;
				break;
			}
			case '#': {
				end = true;
				break;
			}
			default: {
				if(temp != '\n') {
					buffer[i] = temp;
					i++;
				}
				break;
			}
		}
		if(end) {
			break;
		}
	}
	buffer[i] = '\0';
	if(i >= 1) {
		if(isFocuserCommand) {
			focuserCommand(buffer);
		} else {
			flatcapCommand(buffer);
		}
	}
    while(Serial.available()) {
    	Serial.read();
    }
}

void focuserCommand(char* command) {
	String temp = String(command);
	if(temp.startsWith("2")) {
		temp = temp.substring(1);
	}
	String cmd, param;
	int len = temp.length();
	if(len >= 2) {
		cmd = temp.substring(0, 2);
	} else {
		cmd = temp.substring(0, 1);
	}
	if(len > 2) {
		param = temp.substring(2);
	}
	if(param.length()) {
		return;
	}
	// home the motor, hard-coded, ignore parameters since we only have one motor
	if(cmd.equalsIgnoreCase("PH")) {
		stepper.setCurrentPosition(8000);
		stepper.moveTo(0);
	}
	// firmware value, always return "10"
	if(cmd.equalsIgnoreCase("GV")) {
		Serial.print("10#");
	}
	// get the current motor position
	if(cmd.equalsIgnoreCase("GP")) {
		currentPosition = stepper.currentPosition();
		char tempString[6];
		sprintf(tempString, "%04lX#", currentPosition);
		Serial.print(tempString);
	}
	// get the new motor position (target)
	if(cmd.equalsIgnoreCase("GN")) {
		char tempString[6];
		sprintf(tempString, "%04lX#", targetPosition);
		Serial.print(tempString);
	}
	// get the current temperature from DS1820 temperature sensor
	if(cmd.equalsIgnoreCase("GT")) {
		sensors.requestTemperatures();
		float temperature = sensors.getTempCByIndex(0);
		if(temperature > 100 || temperature < -50) {
			temperature = 0;
		}
		int16_t t_int = (int16_t)(temperature * 2);
		char tempString[5];
		sprintf(tempString, "%04X#", (int16_t)t_int);
		Serial.print(tempString);
	}
	// get the temperature coefficient
	if(cmd.equalsIgnoreCase("GC")) {
		Serial.print((byte)tCoeff, HEX);
		Serial.print('#');
	}
	// set the temperature coefficient
	if(cmd.equalsIgnoreCase("SC")) {
		if(param.length() > 4) {
			param = param.substring(param.length() - 4);
		}
		if(param.startsWith("F")) {
			tCoeff = ((0xFFFF - strtol(param.c_str(), NULL, 16)) / -2.0f) - 0.5f;
		} else {
			tCoeff = strtol(param.c_str(), NULL, 16) / 2.0f;
		}
	}
	// motor is moving - 01 if moving, 00 otherwise
	if(cmd.equalsIgnoreCase("GI")) {
		if(stepper.distanceToGo() != 0) {
			Serial.print("01#");
		}
		else {
			Serial.print("00#");
		}
	}
	// set current motor position
	if(cmd.equalsIgnoreCase("SP")) {
		currentPosition = hexstr2long(param);
		stepper.setCurrentPosition(currentPosition);
	}
	// set new motor position
	if(cmd.equalsIgnoreCase("SN")) {
		targetPosition = hexstr2long(param);
	}
	// initiate a move
	if(cmd.equalsIgnoreCase("FG")) {
		stepper.enableOutputs();
		isEnabled = true;
		stepper.moveTo(targetPosition);
	}
	// stop a move
	if(cmd.equalsIgnoreCase("FQ")) {
		stepper.stop();
	}
	// move motor if not done
	if(stepper.distanceToGo() != 0) {
		millisLastMove = millis();
		currentPosition = stepper.currentPosition();
	} else {
		// check if motor wasn't moved for several seconds and save position and disable motors
		if(millis() - millisLastMove > DISABLE_DELAY) {
			if(lastSavedPosition != currentPosition) {
				#ifndef EXTERNAL_EEPROM
				EEPROM.put(FOCUSER_POSITION_ADDRESS, currentPosition);
				EEPROM.commit();
				#else
				eepromWriteLong(FOCUSER_POSITION_ADDRESS, currentPosition, 4);
				#endif
				lastSavedPosition = currentPosition;
			}
			if(isEnabled) {
				stepper.disableOutputs();
				isEnabled = false;
			}
		}
	}
}

void flatcapCommand(char* command) {
	char temp[8];
	char* cmd = command;
    char* dat = command + 1;
	char data[3] = {0};
	strncpy(data, dat, 3);
    switch(*cmd) {
        /*
        Ping device
        Request: >P000#
        Return : *Pid000#
        */
        case 'P': {
            sprintf(temp, "*P%02d000#", deviceId);
            Serial.print(temp);
			break;
        }
		/*
    	Get device status:
    	Request: >S000#
    	Return : *SidMLC#
    	M  = motor status (0 stopped, 1 running)
    	L  = light status (0 off, 1 on)
    	C  = cover status (0 moving, 1 parked, 2 unparked)
        */
        case 'S': {
            sprintf(temp, "*S%02d%01d%01d%01d#", deviceId, motorStatus, lightStatus, coverStatus);
            Serial.print(temp);
			break;
        }
        /*
    	Unpark shutter
    	Request: >O000#
    	Return : *Oid000#
        */
        case 'O': {
    	    setShutter(UNPARKED);
			sprintf(temp, "*O%02d000#", deviceId);
    	    Serial.print(temp);
			break;
        }
        /*
    	Park shutter
    	Request: >C000#
    	Return : *Cid000#
        */
        case 'C': {
    	    setShutter(PARKED);
			sprintf(temp, "*C%02d000#", deviceId);
    	    Serial.print(temp);
			break;
        }
        /*
    	Turn light on
    	Request: >L000#
    	Return : *Lid000#
        */
        case 'L': {
			if(coverStatus == PARKED) {
    	    	ledcWrite(LED, brightness);
				lightStatus = ON;
			}
    	    sprintf(temp, "*L%02d000#", deviceId);
    	    Serial.print(temp);
			break;
        }
        /*
    	Turn light off
    	Request: >D000#
    	Return : *Did000#
        */
        case 'D': {
			ledcWrite(LED, 0);
			lightStatus = OFF;
    	    sprintf(temp, "*D%02d000#", deviceId);
    	    Serial.print(temp);
			break;
        }
        /*
    	Set brightness
    	Request: >Bxxx#
    	xxx = brightness value from 000-255
    	Return : *Bidxxx#
    	xxx = value that brightness was set from 000-255
        */
        case 'B': {
    	    brightness = atoi(data) % 256;
			#ifndef EXTERNAL_EEPROM
			EEPROM.update(BRIGHTNESS_ADDRESS, brightness);
			EEPROM.commit();
			#else
			eepromWriteByte(BRIGHTNESS_ADDRESS, (byte)brightness, true);
			#endif
    	    if(lightStatus == ON && coverStatus == PARKED) {
    	    	ledcWrite(LED, brightness);
            }
    	    sprintf(temp, "*B%02d%03d#", deviceId, brightness);
            Serial.print(temp);
			break;
        }
		/*
    	Set shutter park angle
    	Request: >Zxxx#
    	xxx = angle from 000-360
    	Return : *Zidxxx#
    	xxx = value that park angle was set from 000-360
        */
        case 'Z': {
    	    parkAngle = atoi(data) % 360;
			#ifndef EXTERNAL_EEPROM
			EEPROM.put(PARK_ANGLE_ADDRESS, (uint16_t)parkAngle);
			EEPROM.commit();
			#else
			eepromWriteLong(PARK_ANGLE_ADDRESS, (unsigned long)parkAngle, 2);
			#endif
    	    if(coverStatus == PARKED) {
				moveServo(parkAngle);
            }
    	    sprintf(temp, "*Z%02d%03d#", deviceId, parkAngle);
            Serial.print(temp);
			break;
        }
		/*
    	Set shutter unpark angle
    	Request: >Axxx#
    	xxx = angle from 000-360
    	Return : *Aidxxx#
    	xxx = value that unpark angle was set from 000-360
        */
        case 'A': {
    	    unparkAngle = atoi(data) % 360;
			#ifndef EXTERNAL_EEPROM
			EEPROM.put(UNPARK_ANGLE_ADDRESS, (uint16_t)unparkAngle);
			EEPROM.commit();
			#else
			eepromWriteLong(UNPARK_ANGLE_ADDRESS, (unsigned long)unparkAngle, 2);
			#endif
    	    if(coverStatus == UNPARKED) {
				moveServo(unparkAngle);
            }
    	    sprintf(temp, "*A%02d%03d#", deviceId, unparkAngle);
            Serial.print(temp);
			break;
        }
		/*
    	Get brightness
    	Request: >J000#
    	Return : *Jidxxx#
    	xxx = current brightness value from 000-255
        */
        case 'J': {
            sprintf(temp, "*J%02d%03d#", deviceId, brightness);
            Serial.print(temp);
			break;
        }
		/*
    	Get shutter park angle
    	Request: >K000#
    	Return : *Kidxxx#
    	xxx = value that park angle was set from 000-360
        */
        case 'K': {
            sprintf(temp, "*K%02d%03d#", deviceId, parkAngle);
            Serial.print(temp);
			break;
        }
		/*
    	Get shutter unpark angle
    	Request: >H000#
    	Return : *Hidxxx#
    	xxx = value that unpark angle was set from 000-360
        */
        case 'H': {
            sprintf(temp, "*H%02d%03d#", deviceId, unparkAngle);
            Serial.print(temp);
			break;
        }
        /*
    	Get firmware version
    	Request: >V000#
    	Return : *Vid001#
        */
        case 'V': {
            sprintf(temp, "*V%02d001#", deviceId);
            Serial.print(temp);
			break;
        }
    }
}

void setShutter(int shutter) {
	if(shutter == UNKNOWN) {
		return;
	}
	if(shutter == PARKED) {
		ledcWrite(LED, 0);
		lightStatus = OFF;
		coverStatus = UNKNOWN;
		moveServo(parkAngle);
	} else if(shutter == UNPARKED) {
		coverStatus = UNKNOWN;
		moveServo(unparkAngle);
	}
	coverStatus = shutter;
	#ifndef EXTERNAL_EEPROM
	EEPROM.update(SHUTTER_STATUS_ADDRESS, shutter);
	EEPROM.commit();
	#else
	eepromWriteByte(SHUTTER_STATUS_ADDRESS, (byte)shutter, true);
	#endif
}

void moveServo(int angle) {
	motorStatus = RUNNING;
	int direction = (servoPosition > angle) ? -1 : 1;
	while(abs((int)servoPosition - angle) >= SERVO_INCREMENT) {
		servoPosition += SERVO_INCREMENT * direction;
		servo.write(servoPosition);
		delay(SERVO_DELAY);
	}
	servo.write(angle);
	servoPosition = angle;
	delay(SERVO_DELAY);
	motorStatus = STOPPED;
}

void stepperRun() {
    stepper.run();
}

long hexstr2long(String line) {
    char buf[line.length() + 1];
    line.toCharArray(buf, line.length() + 1);
    return strtol(buf, NULL, 16);
}

#ifdef EXTERNAL_EEPROM

void eepromWriteLong(unsigned int address, unsigned long data, int length) {
	for(int i = 0; i < length; i++) {
		eepromWriteByte(address + i, (data >> (8 * (length - i - 1))) & 0xFF, false);
	}
}

unsigned long eepromReadLong(unsigned int address, int length) {
	byte data[4] = {0};
	#ifdef USE_WC_EEPROM
	digitalWrite(EEPROM_WC, LOW);
	delay(1);
	#endif
	for(int i = 0; i < length; i++) {
		data[4 - length + i] = eepromReadByte(address + i);
	}
	#ifdef USE_WC_EEPROM
	digitalWrite(EEPROM_WC, HIGH);
	#endif
	return (unsigned long)((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | (data[3]));
}

void eepromWriteByte(int address, byte data, bool protectWrite) {
	#ifdef USE_WC_EEPROM
	if(protectWrite) {
		digitalWrite(EEPROM_WC, LOW);
		delay(1);
	}
	#endif
    Wire.beginTransmission(EEPROM_ADDRESS);
    Wire.write(address >> 8);
    Wire.write(address & 0xFF);
    Wire.write(data);
    Wire.endTransmission();
    delay(10);
	#ifdef USE_WC_EEPROM
	if(protectWrite) {
		digitalWrite(EEPROM_WC, HIGH);
	}
	#endif
}

byte eepromReadByte(int address) {
    Wire.beginTransmission(EEPROM_ADDRESS);
    Wire.write(address >> 8);
    Wire.write(address & 0xFF);
    Wire.endTransmission();
    Wire.requestFrom(EEPROM_ADDRESS, (byte)1);
    byte data = 0;
    if(Wire.available()) {
        data = Wire.read();
    }
    return data;
}

#endif