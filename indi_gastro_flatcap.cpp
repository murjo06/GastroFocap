#include <cstring>

#include "libindi/indicom.h"
#include "libindi/connectionplugins/connectionserial.h"

#include "config.h"
#include "indi_gastro_flatcap.h"

static std::unique_ptr<GastroFlatcap> gastroFlatcap(new GastroFlatcap());

GastroFlatcap::GastroFlatcap() : INDI::LightBoxInterface(this, true) {
    setVersion(CDRIVER_VERSION_MAJOR, CDRIVER_VERSION_MINOR);
}

const char *GastroFlatcap::getDefaultName() {
    return "Gastro Flatcap";
}

bool GastroFlatcap::initProperties() {
    // initialize the parent's properties first
    INDI::DefaultDevice::initProperties();

    // initialize the parent's properties first
    initLightBoxProperties(getDeviceName(), MAIN_CONTROL_TAB);
    initDustCapProperties(getDefaultName(), MAIN_CONTROL_TAB);

    // TODO: Add any custom properties you need here.

    // Add debug/simulation/etc controls to the driver.
    addAuxControls();

    setDriverInterface(LIGHTBOX_INTERFACE | DUSTCAP_INTERFACE | AUX_INTERFACE);

    serialConnection = new Connection::Serial(this);
    serialConnection->registerHandshake([&]() { return Handshake(); });
    serialConnection->setDefaultBaudRate(Connection::Serial::B_57600);
    serialConnection->setDefaultPort("/dev/ttyACM0");
    registerConnection(serialConnection);

    return true;
}

void GastroFlatcap::ISGetProperties(const char *dev) {
    INDI::DefaultDevice::ISGetProperties(dev);

    isGetLightBoxProperties(dev);
}

bool GastroFlatcap::updateProperties() {
    INDI::DefaultDevice::updateProperties();

    if(!updateLightBoxProperties()) {
        return false;
    }

    if(isConnected()) {
        // TODO: Call define* for any custom properties only visible when connected.
        defineProperty(&ParkCapSP);
    } else {
        // TODO: Call deleteProperty for any custom properties only visible when connected.
        deleteProperty(ParkCapSP.name);
    }

    return true;
}

bool GastroFlatcap::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) {
    // Make sure it is for us.
    if(dev != nullptr && strcmp(dev, getDeviceName()) == 0) {
        // TODO: Check to see if this is for any of my custom Number properties.
    }

    if(processLightBoxNumber(dev, name, values, names, n)) {
        return true;
    }

    // Nobody has claimed this, so let the parent handle it
    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

bool GastroFlatcap::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) {
    // Make sure it is for us.
    if(dev != nullptr && strcmp(dev, getDeviceName()) == 0) {
        // TODO: Check to see if this is for any of my custom Switch properties.
    }

    if(processLightBoxSwitch(dev, name, states, names, n)) {
        return true;
    }

    // Nobody has claimed this, so let the parent handle it
    return INDI::DefaultDevice::ISNewSwitch(dev, name, states, names, n);
}

bool GastroFlatcap::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) {
    // Make sure it is for us.
    if(dev != nullptr && strcmp(dev, getDeviceName()) == 0) {
        // TODO: Check to see if this is for any of my custom Text properties.
    }

    if(processLightBoxText(dev, name, texts, names, n)) {
        return true;
    }

    // Nobody has claimed this, so let the parent handle it
    return INDI::DefaultDevice::ISNewText(dev, name, texts, names, n);
}

bool GastroFlatcap::ISSnoopDevice(XMLEle *root) {
    // TODO: Check to see if this is for any of my custom Snoops. Fo shizzle.

    snoopLightBox(root);

    return INDI::DefaultDevice::ISSnoopDevice(root);
}

bool GastroFlatcap::saveConfigItems(FILE *fp) {
    saveLightBoxConfigItems(fp);

    // TODO: Call IUSaveConfig* for any custom properties I want to save.

    return INDI::DefaultDevice::saveConfigItems(fp);
}

bool GastroFlatcap::Handshake() {
    if(isSimulation()) {
        LOGF_INFO("Connected successfuly to simulated %s.", getDeviceName());
        return true;
    }

    PortFD = serialConnection->getPortFD();

    return true;
}

void GastroFlatcap::TimerHit() {
    if(!isConnected()) {
        return;
    }

    // TODO: Poll your device if necessary. Otherwise delete this method and it's
    // declaration in the header file.

    LOG_INFO("timer hit");

    // If you don't call SetTimer, we'll never get called again, until we disconnect
    // and reconnect.
    SetTimer(POLLMS);
}

bool GastroFlatcap::SetLightBoxBrightness(uint16_t value) {
    // TODO: Implement your own code to set the brightness of the lightbox.
    // Be sure to return true if successful, or false otherwise.

    INDI_UNUSED(value);

    return false;
}

bool GastroFlatcap::EnableLightBox(bool enable) {
    // TODO: Implement your own code to turn on/off the lightbox.
    // Be sure to return true if successful, or false otherwise.

    INDI_UNUSED(enable);

    return false;
}

IPState DummyDustcap::ParkCap() {
    // TODO: Implement your own code to close the dust cap.
    return IPS_OK;
}

IPState DummyDustcap::UnParkCap() {
    // TODO: Implement your own code to open the dust cap.

    return IPS_OK;
}