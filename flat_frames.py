import sys
import time
import logging
import threading
import PyIndi

EXPOSURE_TIME = 0.01
NUMBER_OF_EXPOSURES = 30

telescope = "EQMod Mount"
ccd = "Canon DSLR EOS 1500D"
flatcap = "Gastro Flatcap"

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
    print(f"No indiserver running on {indiclient.getHost()}:{indiclient.getPort()} - Try to run")
    print("  indiserver indi_simulator_telescope indi_simulator_ccd")
    sys.exit(1)

time.sleep(1)



# * connection

device_telescope = None
telescope_connect = None

device_telescope = indiclient.getDevice(telescope)
while not device_telescope:
    time.sleep(0.5)
    device_telescope = indiclient.getDevice(telescope)

telescope_connect = device_telescope.getSwitch("CONNECTION")
while not telescope_connect:
    time.sleep(0.5)
    telescope_connect = device_telescope.getSwitch("CONNECTION")

if not device_telescope.isConnected():
    print("Failed to connect flatcap, retrying...")
    telescope_connect.reset()
    telescope_connect[0].setState(PyIndi.ISS_ON)
    indiclient.sendNewProperty(telescope_connect)


device_flatcap = None
flatcap_connect = None

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


device_ccd = indiclient.getDevice(ccd)
while not (device_ccd):
    time.sleep(0.5)
    device_ccd = indiclient.getDevice(ccd)

ccd_connect = device_ccd.getSwitch("CONNECTION")
while not (ccd_connect):
    time.sleep(0.5)
    ccd_connect = device_ccd.getSwitch("CONNECTION")
if not (device_ccd.isConnected()):
    ccd_connect.reset()
    ccd_connect[0].setState(PyIndi.ISS_ON)
    indiclient.sendNewProperty(ccd_connect)



# * parking and light

mountParkProperty = device_telescope.getSwitch("TELESCOPE_PARK")
mountParkProperty[0].setState(PyIndi.ISS_ON)
indiclient.sendNewProperty(mountParkProperty)

capParkProperty = device_flatcap.getSwitch("CAP_PARK")
capParkProperty[0].setState(PyIndi.ISS_ON)
indiclient.sendNewProperty(capParkProperty)

flatLightProperty = device_flatcap.getSwitch("FLAT_LIGHT_CONTROL")
flatLightProperty[0].setState(PyIndi.ISS_ON)
indiclient.sendNewProperty(flatLightProperty)


# * exposures

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
    if i + 1 < NUMBER_OF_EXPOSURES:
        ccd_exposure[0].setValue(EXPOSURE_TIME)
        blobEvent.clear()
        indiclient.sendNewProperty(ccd_exposure)
    for blob in ccd_ccd1:
        print("name: ", blob.getName(), " size: ", blob.getSize(), " format: ", blob.getFormat())
        fits = blob.getblobdata()
        print("fits data type: ", type(fits))
    i += 1










print("Disconnecting")
indiclient.disconnectServer()