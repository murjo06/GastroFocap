#include "indi_gastro_flatcap.h"

#include "indicom.h"
#include "connectionplugins/connectionserial.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <termios.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/ioctl.h>

static std::unique_ptr<FlatCap> flatcap(new FlatCap());

#define FLAT_CMD 6
#define FLAT_RES 8
#define FLAT_TIMEOUT 5

#define MIN_ANGLE 0.0
#define MAX_ANGLE 360.0

FlatCap::FlatCap() : LightBoxInterface(this, true), DustCapInterface()
{
    setVersion(1, 1);
}

bool FlatCap::initProperties()
{
    INDI::DefaultDevice::initProperties();

    IUFillText(&StatusT[0], "COVER", "Cover", nullptr);
    IUFillText(&StatusT[1], "LIGHT", "Light", nullptr);
    IUFillText(&StatusT[2], "MOTOR", "Motor", nullptr);
    IUFillTextVector(&StatusTP, StatusT, 3, getDeviceName(), "Status", "Status", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    IUFillText(&FirmwareT[0], "VERSION", "Version", nullptr);
    IUFillTextVector(&FirmwareTP, FirmwareT, 1, getDeviceName(), "Firmware", "Firmware", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    IUFillNumber(&AnglesN[0], "PARK_ANGLE", "Park", "%.0f", MIN_ANGLE, MAX_ANGLE, 5.0, 270.0);
    IUFillNumber(&AnglesN[1], "UNPARK_ANGLE", "Unpark", "%.0f", MIN_ANGLE, MAX_ANGLE, 5.0, 0);

    IUFillNumberVector(&AnglesNP, AnglesN, 2, getDeviceName(), "ANGLES", "Shutter Angles", MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    initDustCapProperties(getDeviceName(), MAIN_CONTROL_TAB);
    initLightBoxProperties(getDeviceName(), MAIN_CONTROL_TAB);

    LightIntensityN[0].min  = 1.0;
    LightIntensityN[0].max  = 255.0;
    LightIntensityN[0].step = 5.0;

    setDriverInterface(AUX_INTERFACE | LIGHTBOX_INTERFACE | DUSTCAP_INTERFACE);

    addAuxControls();

    serialConnection = new Connection::Serial(this);
    serialConnection->registerHandshake([&]()
    {
        return Handshake();
    });
    registerConnection(serialConnection);

    return true;
}

void FlatCap::ISGetProperties(const char *dev)
{
    INDI::DefaultDevice::ISGetProperties(dev);
    isGetLightBoxProperties(dev);
}

bool FlatCap::updateProperties()
{
    INDI::DefaultDevice::updateProperties();

    if (isConnected())
    {
        defineProperty(&ParkCapSP);
        defineProperty(&LightSP);
        defineProperty(&LightIntensityNP);
        defineProperty(&StatusTP);
        defineProperty(&FirmwareTP);
        defineProperty(&AnglesNP);

        updateLightBoxProperties();

        getStartupData();
    }
    else
    {
        deleteProperty(ParkCapSP.name);
        deleteProperty(LightSP.name);
        deleteProperty(LightIntensityNP.name);
        deleteProperty(StatusTP.name);
        deleteProperty(FirmwareTP.name);
        deleteProperty(AnglesNP.name);

        updateLightBoxProperties();
    }

    return true;
}

const char *FlatCap::getDefaultName()
{
    return "Gastro Flatcap";
}

bool FlatCap::Handshake()
{
    if (isSimulation())
    {
        LOGF_INFO("Connected successfully to simulated %s. Retrieving startup data...", getDeviceName());

        SetTimer(getCurrentPollingPeriod());

        syncDriverInfo();

        return true;
    }

    PortFD = serialConnection->getPortFD();
    
    tcflush(PortFD, TCIOFLUSH);

    if (!ping())
    {
        LOG_ERROR("Device ping failed.");
        return false;
    }
    
	syncDriverInfo();

    return true;
}

bool FlatCap::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if(strcmp(name, "ANGLES") == 0) {
        for(int i = 0; i < n; i++) {
            if(strcmp(names[i], "PARK_ANGLE") == 0) {
                setParkAngle((uint16_t)round(values[i]));
            }
            else if(strcmp(names[i], "UNPARK_ANGLE") == 0) {
                setUnparkAngle((uint16_t)round(values[i]));
            }
        }
        return true;
    }
    if (processLightBoxNumber(dev, name, values, names, n))
        return true;

    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

bool FlatCap::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (processLightBoxText(dev, name, texts, names, n))
            return true;
    }

    return INDI::DefaultDevice::ISNewText(dev, name, texts, names, n);
}

bool FlatCap::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (processDustCapSwitch(dev, name, states, names, n))
            return true;

        if (processLightBoxSwitch(dev, name, states, names, n))
            return true;
    }

    return INDI::DefaultDevice::ISNewSwitch(dev, name, states, names, n);
}

bool FlatCap::ISSnoopDevice(XMLEle *root)
{
    snoopLightBox(root);

    return INDI::DefaultDevice::ISSnoopDevice(root);
}

bool FlatCap::saveConfigItems(FILE *fp)
{
    INDI::DefaultDevice::saveConfigItems(fp);

    return saveLightBoxConfigItems(fp);
}

bool FlatCap::ping()
{
    char response[FLAT_RES] = {0};

    if (!sendCommand(">P000", response))
        return false;

    char productString[3] = { 0 };
    snprintf(productString, 3, "%s", response + 2);

    int rc = sscanf(productString, "%hu", &productID);
    if (rc <= 0)
    {
        LOGF_ERROR("Unable to parse input (%s)", response);
        return false;
    }

    // syncDriverInfo() used to be here, not sure if I need it now

    return true;
}

bool FlatCap::getStartupData()
{
    bool rc1 = getFirmwareVersion();
    bool rc2 = getStatus();
    bool rc3 = getBrightness();
    bool rc4 = getParkAngle();
    bool rc5 = getUnparkAngle();

    return (rc1 && rc2 && rc3 && rc4 && rc5);
}

IPState FlatCap::ParkCap()
{
    if (isSimulation())
    {
        simulationWorkCounter = 3;
        return IPS_BUSY;
    }

    char response[FLAT_RES];
    if (!sendCommand(">C000", response))
        return IPS_ALERT;

    char expectedResponse[FLAT_RES];
    snprintf(expectedResponse, FLAT_RES, "*C%02d", productID);

    if (strstr(response, expectedResponse))
    {
        // Set cover status to random value outside of range to force it to refresh
        prevCoverStatus = 10;

        IERmTimer(parkTimeoutID);
        parkTimeoutID = IEAddTimer(30000, parkTimeoutHelper, this);
        return IPS_BUSY;
    }
    else
        return IPS_ALERT;
}

IPState FlatCap::UnParkCap()
{
    if (isSimulation())
    {
        simulationWorkCounter = 3;
        return IPS_BUSY;
    }

    char response[FLAT_RES];
    if (!sendCommand(">O000", response))
        return IPS_ALERT;

    char expectedResponse[FLAT_RES];
    snprintf(expectedResponse, FLAT_RES, "*O%02d", productID);

    if (strstr(response, expectedResponse))
    {
        // Set cover status to random value outside of range to force it to refresh
        prevCoverStatus = 10;

        IERmTimer(unparkTimeoutID);
        unparkTimeoutID = IEAddTimer(30000, unparkTimeoutHelper, this);

        return IPS_BUSY;
    }
    else
        return IPS_ALERT;
}
bool FlatCap::setParkAngle(uint16_t value) {
    if (isSimulation())
    {
        AnglesN[0].value = (double)value;
        IDSetNumber(&AnglesNP, nullptr);
        return true;
    }

    char command[FLAT_CMD] = {0};
    char response[FLAT_RES] = {0};

    snprintf(command, FLAT_CMD, ">Z%03d", value);

    if (!sendCommand(command, response))
        return false;

    char angleString[4] = { 0 };
    snprintf(angleString, 4, "%s", response + 4);

    int angleValue = 0;
    int rc = sscanf(angleString, "%d", &angleValue);

    if (rc <= 0)
    {
        LOGF_ERROR("Unable to parse park angle value (%s)", response);
        return false;
    }

    AnglesN[0].value = (double)angleValue;
    IDSetNumber(&AnglesNP, nullptr);

    return true;
}
bool FlatCap::setUnparkAngle(uint16_t value) {
    if (isSimulation())
    {
        AnglesN[1].value = (double)value;
        IDSetNumber(&AnglesNP, nullptr);
        return true;
    }

    char command[FLAT_CMD] = {0};
    char response[FLAT_RES] = {0};

    snprintf(command, FLAT_CMD, ">A%03d", value);

    if (!sendCommand(command, response))
        return false;

    char angleString[4] = { 0 };
    snprintf(angleString, 4, "%s", response + 4);

    int angleValue = 0;
    int rc = sscanf(angleString, "%d", &angleValue);

    if (rc <= 0)
    {
        LOGF_ERROR("Unable to parse unpark angle value (%s)", response);
        return false;
    }

    AnglesN[1].value = (double)angleValue;
    IDSetNumber(&AnglesNP, nullptr);

    return true;
}
bool FlatCap::getParkAngle()
{
    if (isSimulation())
    {
        return true;
    }

    char response[FLAT_RES] = {0};
    if (!sendCommand(">K000", response))
        return false;

    char angleString[4] = { 0 };
    snprintf(angleString, 4, "%s", response + 4);

    int angleValue = 0;
    int rc = sscanf(angleString, "%d", &angleValue);

    if (rc <= 0)
    {
        LOGF_ERROR("Unable to parse closed angle value (%s)", response);
        return false;
    }

    AnglesN[0].value = (double)angleValue;
    IDSetNumber(&AnglesNP, nullptr);

    return true;
}
bool FlatCap::getUnparkAngle()
{
    if (isSimulation())
    {
        return true;
    }

    char response[FLAT_RES] = {0};
    if (!sendCommand(">H000", response))
        return false;

    char angleString[4] = { 0 };
    snprintf(angleString, 4, "%s", response + 4);

    int angleValue = 0;
    int rc = sscanf(angleString, "%d", &angleValue);

    if (rc <= 0)
    {
        LOGF_ERROR("Unable to parse open angle value (%s)", response);
        return false;
    }

    AnglesN[1].value = (double)angleValue;
    IDSetNumber(&AnglesNP, nullptr);

    return true;
}

bool FlatCap::EnableLightBox(bool enable)
{
    char command[FLAT_CMD];
    char response[FLAT_RES];

    if (ParkCapS[1].s == ISS_ON)
    {
        LOG_ERROR("Cannot control light while cap is unparked.");
        return false;
    }

    if (isSimulation())
        return true;

    if (enable)
        strncpy(command, ">L000", FLAT_CMD);
    else
        strncpy(command, ">D000", FLAT_CMD);

    if (!sendCommand(command, response))
        return false;

    char expectedResponse[FLAT_RES];
    if (enable)
        snprintf(expectedResponse, FLAT_RES, "*L%02d000", productID);
    else
        snprintf(expectedResponse, FLAT_RES, "*D%02d000", productID);

    return (strstr(response, expectedResponse));
}

bool FlatCap::getStatus()
{
    char response[FLAT_RES];

    if (isSimulation())
    {
        if (ParkCapSP.s == IPS_BUSY && --simulationWorkCounter <= 0)
        {
            ParkCapSP.s = IPS_OK;
            IDSetSwitch(&ParkCapSP, nullptr);
            simulationWorkCounter = 0;
        }

        if (ParkCapSP.s == IPS_BUSY)
        {
            response[4] = '1';
            response[6] = '0';
        }
        else
        {
            response[4] = '0';
            // Parked/Closed
            if (ParkCapS[CAP_PARK].s == ISS_ON)
                response[6] = '1';
            else
                response[6] = '2';
        }

        response[5] = (LightS[FLAT_LIGHT_ON].s == ISS_ON) ? '1' : '0';
    }
    else
    {
        if (!sendCommand(">S000", response))
            return false;
    }

    char motorStatus = *(response + 4) - '0';
    char lightStatus = *(response + 5) - '0';
    char coverStatus = *(response + 6) - '0';

    bool statusUpdated = false;

    if (coverStatus != prevCoverStatus)
    {
        prevCoverStatus = coverStatus;

        statusUpdated = true;

        switch (coverStatus)
        {
            case 0:
                IUSaveText(&StatusT[0], "Not Open/Closed");
                break;

            case 1:
                IUSaveText(&StatusT[0], "Closed");
                if (ParkCapSP.s == IPS_BUSY || ParkCapSP.s == IPS_IDLE)
                {
                    IUResetSwitch(&ParkCapSP);
                    ParkCapS[0].s = ISS_ON;
                    ParkCapSP.s   = IPS_OK;
                    LOG_INFO("Cover closed.");
                    IDSetSwitch(&ParkCapSP, nullptr);
                }
                break;

            case 2:
                IUSaveText(&StatusT[0], "Open");
                if (ParkCapSP.s == IPS_BUSY || ParkCapSP.s == IPS_IDLE)
                {
                    IUResetSwitch(&ParkCapSP);
                    ParkCapS[1].s = ISS_ON;
                    ParkCapSP.s   = IPS_OK;
                    LOG_INFO("Cover open.");
                    IDSetSwitch(&ParkCapSP, nullptr);
                }
                break;

            case 3:
                IUSaveText(&StatusT[0], "Timed out");
                break;
        }
    }

    if (lightStatus != prevLightStatus)
    {
        prevLightStatus = lightStatus;

        statusUpdated = true;

        switch (lightStatus)
        {
            case 0:
                IUSaveText(&StatusT[1], "Off");
                if (LightS[0].s == ISS_ON)
                {
                    LightS[0].s = ISS_OFF;
                    LightS[1].s = ISS_ON;
                    IDSetSwitch(&LightSP, nullptr);
                }
                break;

            case 1:
                IUSaveText(&StatusT[1], "On");
                if (LightS[1].s == ISS_ON)
                {
                    LightS[0].s = ISS_ON;
                    LightS[1].s = ISS_OFF;
                    IDSetSwitch(&LightSP, nullptr);
                }
                break;
        }
    }

    if (motorStatus != prevMotorStatus)
    {
        prevMotorStatus = motorStatus;

        statusUpdated = true;

        switch (motorStatus)
        {
            case 0:
                IUSaveText(&StatusT[2], "Stopped");
                break;

            case 1:
                IUSaveText(&StatusT[2], "Running");
                break;
        }
    }

    if (statusUpdated)
        IDSetText(&StatusTP, nullptr);

    return true;
}

bool FlatCap::getFirmwareVersion()
{
    if (isSimulation())
    {
        IUSaveText(&FirmwareT[0], "Simulation");
        IDSetText(&FirmwareTP, nullptr);
        return true;
    }

    char response[FLAT_RES] = {0};
    if (!sendCommand(">V000", response))
        return false;

    char versionString[4] = { 0 };
    snprintf(versionString, 4, "%s", response + 4);
    IUSaveText(&FirmwareT[0], versionString);
    IDSetText(&FirmwareTP, nullptr);

    return true;
}

void FlatCap::TimerHit()
{
    if (!isConnected())
        return;

    getStatus();

    // parking or unparking timed out, try again
    if (ParkCapSP.s == IPS_BUSY && !strcmp(StatusT[0].text, "Timed out"))
    {
        if (ParkCapS[0].s == ISS_ON)
            ParkCap();
        else
            UnParkCap();
    }

    SetTimer(getCurrentPollingPeriod());
}

bool FlatCap::getBrightness()
{
    if (isSimulation())
    {
        return true;
    }

    char response[FLAT_RES] = {0};
    if (!sendCommand(">J000", response))
        return false;

    char brightnessString[4] = { 0 };
    snprintf(brightnessString, 4, "%s", response + 4);

    int brightnessValue = 0;
    int rc = sscanf(brightnessString, "%d", &brightnessValue);

    if (rc <= 0)
    {
        LOGF_ERROR("Unable to parse brightness value (%s)", response);
        return false;
    }

    if (brightnessValue != prevBrightness)
    {
        prevBrightness = brightnessValue;
        LightIntensityN[0].value = brightnessValue;
        IDSetNumber(&LightIntensityNP, nullptr);
    }
    
    return true;
}

bool FlatCap::SetLightBoxBrightness(uint16_t value)
{
    if (isSimulation())
    {
        LightIntensityN[0].value = value;
        IDSetNumber(&LightIntensityNP, nullptr);
        return true;
    }

    char command[FLAT_CMD] = {0};
    char response[FLAT_RES] = {0};

    snprintf(command, FLAT_CMD, ">B%03d", value);

    if (!sendCommand(command, response))
        return false;

    char brightnessString[4] = { 0 };
    snprintf(brightnessString, 4, "%s", response + 4);

    int brightnessValue = 0;
    int rc = sscanf(brightnessString, "%d", &brightnessValue);

    if (rc <= 0)
    {
        LOGF_ERROR("Unable to parse brightness value (%s)", response);
        return false;
    }

    if (brightnessValue != prevBrightness)
    {
        prevBrightness = brightnessValue;
        LightIntensityN[0].value = brightnessValue;
        IDSetNumber(&LightIntensityNP, nullptr);
    }

    return true;
}

bool FlatCap::sendCommand(const char *command, char *response)
{
    int nbytes_read = 0, nbytes_written = 0, rc = -1;
    char buffer[FLAT_CMD + 1] = {0};
    char errstr[MAXRBUF] = {0};
    tcflush(PortFD, TCIOFLUSH);
    if(isSimulation())
    {
        return true;
    }
    else
    {
        snprintf(buffer, FLAT_CMD + 1, "%s\n", command);
        if((rc = tty_write(PortFD, buffer, FLAT_CMD, &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(rc, errstr, MAXRBUF);
            LOGF_ERROR("%s write error: %s", command, errstr);
            return false;
        }
        if((rc = tty_nread_section(PortFD, response, FLAT_RES + 1, 0xA, FLAT_TIMEOUT, &nbytes_read)) != TTY_OK)
        {
            tty_error_msg(rc, errstr, MAXRBUF);
            LOGF_ERROR("%s read error: %s", command, errstr);
            return false;
        }
        response[nbytes_read - 1] = 0;

        return true;
    }
    return false;
}

void FlatCap::parkTimeoutHelper(void *context)
{
    static_cast<FlatCap*>(context)->parkTimeout();
}

void FlatCap::unparkTimeoutHelper(void *context)
{
    static_cast<FlatCap*>(context)->unparkTimeout();
}

void FlatCap::parkTimeout()
{
    if (ParkCapSP.s == IPS_BUSY)
    {
        LOG_WARN("Parking cap timed out. Retrying...");
        ParkCap();
    }
}

void FlatCap::unparkTimeout()
{
    if (ParkCapSP.s == IPS_BUSY)
    {
        LOG_WARN("UnParking cap timed out. Retrying...");
        UnParkCap();
    }
}