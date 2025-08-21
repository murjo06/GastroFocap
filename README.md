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

Upload the [esp32.ino](esp32.ino) file to an ESP32 S3. If your module has PSRAM, you'll have to change the pins, since the PSRAM uses pins 35, 36, and 37 on the S3. The PCB won't work in that case.


Alternatively, you can use an Arduino Nano and upload the [arduino.ino](arduino.ino) file, but this only contains flatcap functionality.


This firmware uses the M24C64 EEPROM IC by default, but that can be changed to use the ESP32 S3's own "EEPROM", although I don't recommend it. Since EEPROM has limited write cycles (to be fair that's about 4 million for the M24C64), it's best to change the exposure length of flats through different filters, rather than changing the brightness value for each filter (for the Arduino version of the firmware this becomes slightly more applicable, since it has only 100000 write cycles). Ekos, as far as I know, doesn't even offer a way to change flatcap brightness in different filters.


The communication protocol requests and responses are located in [communication.md](communication.md).


I've provided gerbers for the PCB inside the [pcb_gerbers](/pcb_gerbers/) folder. You may use these (as well as other files in this repository) as per the [license file](/LICENSE). I recommend using JLCPCB for the manufacturing, since the bom is suited for it. You can leave all settings as default, just make sure to correct all the errors in the pick-and-place file, since there are many. JLCPCB allows you to do that in the website. You'll also need some additional components that aren't in the BOM (rather [here](/pcb_gerbers/additional_bom.md)) and have to be manually soldered since it's cheaper that way.