# Gastro Flatcap INDI driver with Arduino Nano firmware

### Installing the INDI driver
```sh
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug ..
make
sudo make install
```


### Uploading the firmware

Upload the [firmware.ino](firmware/firmware.ino) file to an Arduino Nano. Alternatively, use the legacy firmware [alnitak.ino](firmware/legacy/alnitak.ino). You will also have to calibrate the cover park and unpark angles with [calibrate.ino](firmware/legacy/calibrate.ino). If you want to use the legacy firmware, use the Flip-Flat driver in INDI.


The firmware uses the Arduino EEPROM, which is limited to about 100000 read/write cycles, so it is advised to change the exposure length of flats, instead of changing the brightness of the panel when using different fiters.