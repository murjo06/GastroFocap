#include <Servo.h>

Servo servo;
String buffer;

void setup() {
    servo.attach(9);
    Serial.begin(9600);
}

void loop() {
    if(Serial.available() > 0) {
        buffer = Serial.readString();
        int position = buffer.toInt();
        if(position < 0 || position > 300) {
            Serial.print("Invalid position");
        } else {
            Serial.print("Moving to ");
            Serial.println(buffer.toInt());
            servo.write(position);
        }
    }
}