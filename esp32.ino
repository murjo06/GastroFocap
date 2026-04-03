#include <ESPServo.h>
#include <AccelStepperEncoder.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TMCStepper.h>
#include <Streaming.h>
#include <Wire.h>

#define EXTERNAL_EEPROM
//#define USE_WC_EEPROM

#ifndef EXTERNAL_EEPROM
#include <EEPROM.h>
#endif

#define COUNTS_PER_REVOLUTION (1 << 14)
#define ENCODER_MOTOR_RATIO 30.4	// number of encoder counts that equal one step

#define BUFFER_SIZE 32

#define LED 1
#define SERVO 38

#define SDA 37
#define SCL 36
#define EEPROM_WC 35				// write control for M24C64, drive high to prevent writing, toggled by USE_WC_EEPROM
#define EEPROM_ADDRESS 0b1010000
#define ENCODER_ADDRESS 0b0000110

#define EN 3						// enable
#define DIR 16						// direction
#define STEP 17						// step
#define TX 8						// receive pin
#define RX 18						// transmit pin
#define TMC_ADDRESS 0b00	 		// TMC2209 driver address according to MS1 and MS2
#define R_SENSE 0.1f				// my board uses R100 resistors because of the JLC parts library

#define RMS_CURRENT 500

#define TEMP 13

#define STEPPER_SPEED 25
#define STEPPER_ACCELERATION 25

#define SERVO_INCREMENT 1			// in degrees
#define SERVO_INTERVAL 20			// delay in ms after each servo increment, speed of the servo can be calculated by
                                    // SERVO_INCREMENT / SERVO_INTERVAL, result is in deg/ms

#define DISABLE_DELAY 15000

enum lightStatuses {
	OFF,
	ON
};

enum shutterStatuses {
	PARKED,
	UNPARKED,
	PARKING,
	UNPARKING
};

//EEPROM storage is in little-endian, since this is what the ESP32 uses normally
enum addresses {
	BRIGHTNESS_ADDRESS = 0,
	PARK_ANGLE_ADDRESS = 1,
	UNPARK_ANGLE_ADDRESS = 3,
	SHUTTER_STATUS_ADDRESS = 5,
	STEPPER_OFFSET_ADDRESS = 6,
    STEPPER_POSITION_ADDRESS = 8
};

uint8_t lightStatus = OFF;
uint8_t shutterStatus = PARKED;
uint8_t brightness = 255;
uint16_t parkAngle = 0;
uint16_t unparkAngle = 0;

AccelStepperEncoder stepper(AccelStepperEncoder::DRIVER, STEP, DIR);

TMC2209Stepper TMCdriver(&Serial2, R_SENSE, TMC_ADDRESS);

OneWire oneWire(TEMP);
DallasTemperature sensors(&oneWire);

ESPServo servo;

float temperatureCoefficient = 1.5f;		// calculated expansion coefficient in steps/K (scope dependent)

uint32_t lastSavedPosition = 0;
int counts = 0;
int32_t encoderPosition = 0;
uint32_t millisLastMove = 0;
bool isEnabled = false;
bool movingAllowed = false;
int16_t stepperOffset = 0;

bool temperatureCompensation = false;

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
	Wire.begin(SDA, SCL);
	Wire.setClock(100000);
	#ifndef EXTERNAL_EEPROM
	EEPROM.begin(10);
	EEPROM.get(PARK_ANGLE_ADDRESS, parkAngle);
	EEPROM.get(UNPARK_ANGLE_ADDRESS, unparkAngle);
	brightness = EEPROM.read(BRIGHTNESS_ADDRESS);
	shutterStatus = EEPROM.read(SHUTTER_STATUS_ADDRESS);
	EEPROM.get(FOCUSER_POSITION_ADDRESS, encoderPosition);
	turns = EEPROM.read(ENCODER_TURNS_ADDRESS);				// TODO: change eeprom
	counts = readEncoderCounts();
	setEncoderTurns((EEPROM.read(ENCODER_COUNTS_ADDRESS) << 8 ) | (EEPROM.read(ENCODER_COUNTS_ADDRESS + 1)));
	#else
	#ifdef USE_WC_EEPROM
	pinMode(EEPROM_WC, OUTPUT);
	digitalWrite(EEPROM_WC, HIGH);
	#endif
	parkAngle = (uint16_t)(eepromReadLong(PARK_ANGLE_ADDRESS, 2) % 360);

	unparkAngle = (uint16_t)(eepromReadLong(UNPARK_ANGLE_ADDRESS, 2) % 360);

	brightness = (uint8_t)(eepromReadByte(BRIGHTNESS_ADDRESS) % 256);
	shutterStatus = (uint8_t)eepromReadByte(SHUTTER_STATUS_ADDRESS);

	stepperOffset = (int16_t)eepromReadLong(STEPPER_OFFSET_ADDRESS, 2);
	stepper.currentPosition = static_cast<int32_t>(eepromReadLong(STEPPER_POSITION_ADDRESS, 4));
	#endif
	servo.attach(SERVO, 0, 270);
	servo.sync((shutterStatus == PARKED) ? parkAngle : unparkAngle);
	servo.setSpeed(SERVO_INCREMENT, SERVO_INTERVAL);

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
	stepper.setPinsInverted(true, false, true);		// dir, step, en
	stepper.setEnablePin(EN);
	stepper.disableOutputs();

	millisLastMove = millis();

	lastSavedPosition = stepper.currentPosition;
	stepper.targetPosition = stepper.currentPosition;

	sensors.begin();
	delay(500);
}

void loop() {
	stepper.currentPosition = (int32_t)(0.5 + getEncoderPosition() / ENCODER_MOTOR_RATIO);
	if(movingAllowed) {
		stepper.run();
	}
	servo.run();
	if(!servo.isRunning()) {
		if(shutterStatus == PARKING) {
			shutterStatus = PARKED;
		} else if(shutterStatus == UNPARKING) {
			shutterStatus = UNPARKED;
		}
	}
	if(stepper.distanceToGo() != 0) {
		millisLastMove = millis();
	} else {
		if(millis() - millisLastMove > DISABLE_DELAY) {
			if(lastSavedPosition != stepper.currentPosition && movingAllowed) {
				movingAllowed = false;
				#ifndef EXTERNAL_EEPROM
				EEPROM.put(ENCODER_COUNTS_ADDRESS, counts);
				EEPROM.update(ENCODER_TURNS_ADDRESS, turns);
				EEPROM.commit();
				#else
				eepromWriteLong(STEPPER_POSITION_ADDRESS, stepper.currentPosition, 4);
				eepromWriteLong(STEPPER_OFFSET_ADDRESS, stepperOffset, 2);
				#endif
				lastSavedPosition = stepper.currentPosition;
			}
			if(isEnabled) {
				stepper.disableOutputs();
				isEnabled = false;
			}
		}
	}
    if(Serial.available() < 3) {
		return;
	}
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
	int i = 0;
	bool isFocuserCommand = false;
	bool read = false;
	while(Serial.available()) {
		if(i >= BUFFER_SIZE - 2) {
			break;
		}
		char temp = Serial.read();
		bool end = false;
		switch(temp) {
			case ':': {
				isFocuserCommand = true;
				read = true;
				break;
			}
			case '>': {
				isFocuserCommand = false;
				read = true;
				break;
			}
			case '#': {
				end = true;
				read = false;
				break;
			}
			default: {
				if(temp != '\n' && read) {
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
	if(isFocuserCommand && i > 0) {
		focuserCommand(buffer);
	} else if(i > 3) {
		flatcapCommand(buffer);
	}
}

void focuserCommand(char* command) {
	String commandString = String(command);
	String cmd, param;
	int length = commandString.length();
	if(length >= 2) {
		cmd = commandString.substring(0, 2);
	} else {
		cmd = commandString.substring(0, 1);
	}
	if(length > 2) {
		param = commandString.substring(2);
	}
	if(cmd.equals("GP")) {		// get the current motor position
		char temp[6];
		sprintf(temp, "%04lx#", stepper.currentPosition - stepperOffset);
		Serial.print(temp);
	} else if(cmd.equals("GN")) {		// get the target motor position
		char temp[6];
		sprintf(temp, "%04lx#", stepper.targetPosition - stepperOffset);
		Serial.print(temp);
	} else if(cmd.equals("GT")) {		// get the current temperature from DS1820 temperature sensor
		sensors.requestTemperatures();
		int32_t rawTemperature = sensors.getTempByIndex(0);
		char temp[6];
		sprintf(temp, "%04x#", (rawTemperature >= -7040 || rawTemperature <= 16000) ? ((uint16_t)(rawTemperature + (1 << 15))) : 0);
		Serial.print(temp);
	} else if(cmd.equals("GC")) {		// get the temperature coefficient
		char temp[6];
		sprintf(temp, "%04x#", (uint16_t)(temperatureCoefficient * 256.0f));
		Serial.print(temp);
	} else if(cmd.equals("SC")) {		// set the temperature coefficient
		temperatureCoefficient = (float)hexStringToLong(param) / 256.0f;		// TODO: specify degree of precision
	} else if(cmd.equals("GI")) {		// motor is moving - 01 if moving, 00 otherwise
		Serial.print(stepper.isRunning() ? "01#" : "00#");
	} else if(cmd.equals("SP")) {		// sync motor
		stepperOffset += hexStringToLong(param) - stepper.currentPosition;
	} else if(cmd.equals("SN")) {		// set target motor position
		stepper.enableOutputs();
		isEnabled = true;
		movingAllowed = true;
		stepper.moveTo(hexStringToLong(param) + stepperOffset);
	} else if(cmd.equals("FQ")) {		// stop a move
		stepper.stop();
		stepper.disableOutputs();
		isEnabled = false;
		movingAllowed = false;
	} else if(cmd.equals("GE")) {		// get encoder counts
		char temp[6];
		sprintf(temp, "%04x#", readEncoderCounts());
		Serial.print(temp);
	} else if(cmd.equals("TC")) {		// toggle temperature compensation, 1 to enable, 0 to disable
		temperatureCompensation = param.startsWith("1");
	}
}

void flatcapCommand(char* command) {
	char temp[9] = {0};
    char* dat = command + 1;
	char data[3] = {0};
	strncpy(data, dat, 3);
    switch(*command) {
        /*
        Ping device
        Request: >P000#
        Return : *P000#
        */
        case 'P': {
            Serial.print("*P000#");
			break;
        }
		/*
    	Get device status:
    	Request: >S000#
    	Return : *SFLC#
		F  = focuser (0 still, 1 moving)
    	L  = light status (0 off, 1 on)
    	C  = shutter status (0 parked, 1 unparked, 2 parking, 3 unparking)
        */
        case 'S': {
            sprintf(temp, "*S0%1d%1d#", lightStatus, shutterStatus);
            Serial.print(temp);
			break;
        }
        /*
    	Unpark shutter
    	Request: >O000#
    	Return : *O000#
        */
        case 'O': {
    	    setShutter(UNPARKED);
    	    Serial.print(">O000#");
			break;
        }
        /*
    	Park shutter
    	Request: >C000#
    	Return : *C000#
        */
        case 'C': {
    	    setShutter(PARKED);
    	    Serial.print("*C000#");
			break;
        }
        /*
    	Turn light on
    	Request: >L000#
    	Return : *L000#
        */
        case 'L': {
			if(shutterStatus == PARKED) {
    	    	ledcWrite(LED, brightness);
				lightStatus = ON;
			}
    	    Serial.print("*L000#");
			break;
        }
        /*
    	Turn light off
    	Request: >D000#
    	Return : *D000#
        */
        case 'D': {
			ledcWrite(LED, 0);
			lightStatus = OFF;
    	    Serial.print("*D000#");
			break;
        }
        /*
    	Set brightness
    	Request: >Bxxx#
    	xxx = brightness from 000-255
    	Return : *Bxxx#
        */
        case 'B': {
    	    brightness = atoi(data) % 256;
			#ifndef EXTERNAL_EEPROM
			EEPROM.update(BRIGHTNESS_ADDRESS, brightness);
			EEPROM.commit();
			#else
			eepromWriteByte(BRIGHTNESS_ADDRESS, (byte)brightness, false);
			#endif
    	    if(lightStatus == ON && shutterStatus == PARKED) {
    	    	ledcWrite(LED, brightness);
            }
    	    sprintf(temp, "*B%03d#", brightness);
            Serial.print(temp);
			break;
        }
		/*
    	Set shutter park angle
    	Request: >Zxxx#
    	xxx = park angle from 000-360
    	Return : *Zxxx#
        */
        case 'Z': {
    	    parkAngle = atoi(data) % 360;
			#ifndef EXTERNAL_EEPROM
			EEPROM.put(PARK_ANGLE_ADDRESS, (uint16_t)parkAngle);
			EEPROM.commit();
			#else
			eepromWriteLong(PARK_ANGLE_ADDRESS, (uint32_t)parkAngle, 2);
			#endif
    	    if(shutterStatus == PARKED || shutterStatus == PARKING) {
				servo.move(parkAngle);
            }
    	    sprintf(temp, "*Z%03d#", parkAngle);
            Serial.print(temp);
			break;
        }
		/*
    	Set shutter unpark angle
    	Request: >Axxx#
    	xxx = unpark angle from 000-360
    	Return : *Axxx#
        */
        case 'A': {
    	    unparkAngle = atoi(data) % 360;
			#ifndef EXTERNAL_EEPROM
			EEPROM.put(UNPARK_ANGLE_ADDRESS, (uint16_t)unparkAngle);
			EEPROM.commit();
			#else
			eepromWriteLong(UNPARK_ANGLE_ADDRESS, (uint32_t)unparkAngle, 2);
			#endif
    	    if(shutterStatus == UNPARKED || shutterStatus == UNPARKING) {
				servo.move(unparkAngle);
            }
    	    sprintf(temp, "*A%03d#", unparkAngle);
            Serial.print(temp);
			break;
        }
		/*
    	Get brightness
    	Request: >J000#
    	Return : *Jxxx#
    	xxx = current brightness from 000-255
        */
        case 'J': {
            sprintf(temp, "*J%03d#", brightness);
            Serial.print(temp);
			break;
        }
		/*
    	Get shutter park angle
    	Request: >K000#
    	Return : *Kxxx#
    	xxx = current park angle from 000-360
        */
        case 'K': {
            sprintf(temp, "*K%03d#", parkAngle);
            Serial.print(temp);
			break;
        }
		/*
    	Get shutter unpark angle
    	Request: >H000#
    	Return : *Hxxx#
    	xxx = current unpark angle from 000-360
        */
        case 'H': {
            sprintf(temp, "*H%03d#", unparkAngle);
            Serial.print(temp);
			break;
        }
        /*
    	Get firmware version
    	Request: >V000#
    	Return : *V001#
        */
        case 'V': {
            Serial.print("*V002#");
			break;
        }
    }
}

void setShutter(int shutter) {
	if(shutter != PARKED && shutter != UNPARKED) {
		return;
	}
	if(shutter == PARKED) {
		ledcWrite(LED, 0);
		lightStatus = OFF;
		shutterStatus = PARKING;
		servo.move(parkAngle);
	} else if(shutter == UNPARKED) {
		shutterStatus = UNPARKING;
		servo.move(unparkAngle);
	}
	#ifndef EXTERNAL_EEPROM
	EEPROM.update(SHUTTER_STATUS_ADDRESS, shutter);
	EEPROM.commit();
	#else
	eepromWriteByte(SHUTTER_STATUS_ADDRESS, (byte)shutter, false);
	#endif
}

uint32_t hexStringToLong(String str) {
	char buffer[str.length() + 1];
  	str.toCharArray(buffer, str.length() + 1);
  	return (uint32_t)strtol(buffer, NULL, 16);
}

int readEncoderCounts() {
    Wire.beginTransmission(ENCODER_ADDRESS);
    Wire.write(0x03);
    Wire.endTransmission(false);
    Wire.requestFrom((int)ENCODER_ADDRESS, 2);
    if(Wire.available() < 2) {
        return -1;
    }
    int angle_h = Wire.read();
    int angle_l = Wire.read();
	if(angle_h < 0 || angle_l < 0) {
		return -2;
	}
    return (angle_h << 6) | (angle_l >> 2);
}

int32_t getEncoderPosition() {
	int newCount = readEncoderCounts();
    for(int i = 0; i < 3 && newCount > COUNTS_PER_REVOLUTION || newCount < 0; i++, newCount = readEncoderCounts()) {}		// try four times
    if(newCount < 0 || newCount > COUNTS_PER_REVOLUTION) {
        return 0;
    }
    int delta = newCount - counts;
    if(delta > (COUNTS_PER_REVOLUTION >> 1)) {
        delta -= COUNTS_PER_REVOLUTION;
    } else if(delta < -(COUNTS_PER_REVOLUTION >> 1)) {
        delta += COUNTS_PER_REVOLUTION;
    }
    counts = newCount;
	encoderPosition += delta;
	return encoderPosition;
}

#ifdef EXTERNAL_EEPROM

void eepromWriteLong(uint16_t address, uint32_t data, int length) {
	#ifdef USE_WC_EEPROM
	digitalWrite(EEPROM_WC, LOW);
	delay(1);
	#endif
	for(int i = 0; i < length; i++) {
		eepromWriteByte(address + i, (data >> (8 * (length - i - 1))) & 0xFF, false);
	}
	#ifdef USE_WC_EEPROM
	digitalWrite(EEPROM_WC, HIGH);
	#endif
}

uint32_t eepromReadLong(uint16_t address, int length) {
	byte data[4] = {0};
	for(int i = 0; i < length; i++) {
		data[4 - length + i] = eepromReadByte(address + i);
	}
	return (uint32_t)((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | (data[3]));
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