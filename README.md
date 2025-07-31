# Gastro Focap INDI driver with firmware

### Installing the INDI driver
```sh
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug ..
make
sudo make install
```


### Uploading the firmware

Upload the [esp32.ino](esp32.ino) file to an ESP32 S3. If your module has PSRAM, you'll have to change the pins, since the PSRAM uses pins 35, 36, and 37 on the S3.


Alternatively, you can use an Arduino Nano and upload the [arduino.ino](arduino.ino) file, but this only contains flatcap functionality.


This firmware uses the M24C64 EEPROM IC by default, but that can be changed to use the ESP32 S3's own "EEPROM", although I don't recommend it. Since EEPROM has limited write cycles (to be fair that's about 4 million for the M24C64), it's best to change the exposure length of flats through different filters, rather than changing the brightness value for each filter (for the Arduino version of the firmware this becomes slightly more applicable, since it has only 100000 write cycles).


The communication protocol requests and responses are located in [communications.md](communication.md).