import sys
import time
import logging
import threading
import PyIndi
import datetime
import os

#
#   This is a script for automated flatcap functionality, first parking the mount and cap, turning the light on, taking some exposures
#   and turning the light off. The light uses the FLAT_LIGHT_CONTROL default INDI property, not just setting the brightness
#   to 0 as some scripts do.
# 	This requires having PyINDI installed, I recommend pulling it from source below and running `python setup.py install`,
#	as the pip repo doesn't work on newer versions.
#
#   Make sure to set the brightness beforehand and set EXPOSURE_TIME so that the histogram peak is at around 1/3 of the whole axis.
#	Don't set it too low, the whole sensor has to be somewhat equally illuminated for the flats to come out well.
#   Set NUMBER_OF_EXPOSURES to some amount, I use 20, doesn't really matter.
#
#   Make sure to change the telescope, ccd and flatcap to whatever names you have, just copy the names in the INDI control panel.
#	The destination is self explanatory, I have it set so it uses today's date.
#
#	This script is based (more like copied) from the pyindi-client examples (https://github.com/indilib/pyindi-client).
#

EXPOSURE_TIME = 0.01
NUMBER_OF_EXPOSURES = 20

telescope = "EQMod Mount"
ccd = "Canon DSLR EOS 1500D"
flatcap = "Gastro Flatcap"

destination = f"../Flats/{datetime.datetime.today().strftime('%Y-%m-%d')}"

os.makedirs(destination, exist_ok=True)

class IndiClient(PyIndi.BaseClient):
    def __init__(self):
        super(IndiClient, self).__init__()

    def updateProperty(self, prop):
        global blobEvent
        if prop.getType() == PyIndi.INDI_BLOB:
            print("New BLOB ", prop.getName())
            blobEvent.set()

logging.basicConfig(format = '%(asctime)s %(message)s', level = logging.INFO)

indiclient=IndiClient()
indiclient.setServer("localhost", 7624)

if not indiclient.connectServer():
    print(f"No indiserver running on {indiclient.getHost()}:{indiclient.getPort()}")
    sys.exit(1)

time.sleep(1)



# * connection

device_telescope = indiclient.getDevice(telescope)
while not device_telescope:
    time.sleep(0.5)
    device_telescope = indiclient.getDevice(telescope)

telescope_connect = device_telescope.getSwitch("CONNECTION")
while not telescope_connect:
    time.sleep(0.5)
    telescope_connect = device_telescope.getSwitch("CONNECTION")

if not device_telescope.isConnected():
    print("Failed to connect telescope, retrying...")
    telescope_connect.reset()
    telescope_connect[0].setState(PyIndi.ISS_ON)
    indiclient.sendNewProperty(telescope_connect)

time.sleep(0.5)

device_flatcap = indiclient.getDevice(flatcap)
while not device_flatcap:
    time.sleep(0.5)
    device_flatcap = indiclient.getDevice(flatcap)

flatcap_connect = device_flatcap.getSwitch("CONNECTION")
while not flatcap_connect:
    time.sleep(0.5)
    flatcap_connect = device_flatcap.getSwitch("CONNECTION")

if not device_flatcap.isConnected():
    print("Failed to connect flatcap, retrying...")
    flatcap_connect.reset()
    flatcap_connect[0].setState(PyIndi.ISS_ON)
    indiclient.sendNewProperty(flatcap_connect)

time.sleep(0.5)

device_ccd = indiclient.getDevice(ccd)
while not (device_ccd):
    time.sleep(0.5)
    device_ccd = indiclient.getDevice(ccd)

ccd_connect = device_ccd.getSwitch("CONNECTION")
while not (ccd_connect):
    time.sleep(0.5)
    ccd_connect = device_ccd.getSwitch("CONNECTION")
if not (device_ccd.isConnected()):
    print("Failed to connect ccd, retrying...")
    ccd_connect.reset()
    ccd_connect[0].setState(PyIndi.ISS_ON)
    indiclient.sendNewProperty(ccd_connect)

time.sleep(0.5)


# * parking and light

mountParkProperty = device_telescope.getSwitch("TELESCOPE_PARK")
mountParkProperty[0].setState(PyIndi.ISS_ON)
indiclient.sendNewProperty(mountParkProperty)

print("Parking telescope...")

time.sleep(10)

capParkProperty = device_flatcap.getSwitch("CAP_PARK")
capParkProperty[0].setState(PyIndi.ISS_ON)
indiclient.sendNewProperty(capParkProperty)

print("Parking cap...")

time.sleep(10)

flatLightProperty = device_flatcap.getSwitch("FLAT_LIGHT_CONTROL")
flatLightProperty[0].setState(PyIndi.ISS_ON)
indiclient.sendNewProperty(flatLightProperty)

time.sleep(3)


# * exposures

print("Starting exposures")

ccd_exposure = device_ccd.getNumber("CCD_EXPOSURE")
while not (ccd_exposure):
    time.sleep(0.5)
    ccd_exposure = device_ccd.getNumber("CCD_EXPOSURE")

ccd_active_devices = device_ccd.getText("ACTIVE_DEVICES")
while not (ccd_active_devices):
    time.sleep(0.5)
    ccd_active_devices = device_ccd.getText("ACTIVE_DEVICES")
ccd_active_devices[0].setText(telescope)
indiclient.sendNewProperty(ccd_active_devices)

indiclient.setBLOBMode(PyIndi.B_ALSO, ccd, "CCD1")

ccd_ccd1 = device_ccd.getBLOB("CCD1")
while not ccd_ccd1:
    time.sleep(0.5)
    ccd_ccd1 = device_ccd.getBLOB("CCD1")

blobEvent = threading.Event()
blobEvent.clear()
i = 0
ccd_exposure[0].setValue(EXPOSURE_TIME)
indiclient.sendNewProperty(ccd_exposure)
while i < NUMBER_OF_EXPOSURES:
    blobEvent.wait()
    time.sleep(0.5)
    if i + 1 < NUMBER_OF_EXPOSURES:
        ccd_exposure[0].setValue(EXPOSURE_TIME)
        blobEvent.clear()
        indiclient.sendNewProperty(ccd_exposure)
    for blob in ccd_ccd1:
        print("Name: ", blob.getName(), " size: ", blob.getSize(), " format: ", blob.getFormat())
        fits = blob.getblobdata()
        print("Fits data type: ", type(fits))
        with open(f"{destination}/{i:03}.cr2", "wb") as file:
            file.write(blob.getblobdata())
    i += 1
    
time.sleep(5)

flatLightProperty = device_flatcap.getSwitch("FLAT_LIGHT_CONTROL")
flatLightProperty[0].setState(PyIndi.ISS_OFF)
indiclient.sendNewProperty(flatLightProperty)



print("Disconnecting")
indiclient.disconnectServer()