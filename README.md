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

Upload the [firmware.ino](firmware/firmware.ino) file to an Arduino Nano.


The firmware uses the Arduino EEPROM, which is limited to about 100000 read/write cycles, so it is advised to change the exposure length of flats, instead of changing the brightness of the panel when using different fiters.


If you have to use exposure times of about 1/250 s or less (ideally never go this low), you should increase the PWM frequency of the LED pin (pins 5 and 6 have 980 Hz, you want at least a few full PWM cycles for each flat, but it can get corrected when stacking. Mathematically the frequency doesn't matter if you use median stacking, but I wouldn't recommend using too short exposure times).