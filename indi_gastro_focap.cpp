#include "indi_gastro_focap.h"

#include "indicom.h"
#include "connectionplugins/connectionserial.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <termios.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/ioctl.h>

static std::unique_ptr<Focap> focap(new Focap());

#define FLAT_CMD 6
#define FLAT_RES 8
#define FLAT_TIMEOUT 5
#define FLAT_MOTOR_TIMEOUT 10

#define MIN_ANGLE 0.0
#define MAX_ANGLE 360.0

Focap::Focap() : LightBoxInterface(this), DustCapInterface(this), FocuserInterface(this)
{
    setVersion(1, 1);

    FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_CAN_ABORT | FOCUSER_CAN_SYNC);
}

bool Focap::initProperties()
{
    INDI::DefaultDevice::initProperties();
    FI::initProperties(FOCUSER_TAB);
    DI::initProperties(FLATCAP_TAB);
    LI::initProperties(FLATCAP_TAB, CAN_DIM);

    IUFillText(&StatusT[0], "COVER", "Cover", nullptr);
    IUFillText(&StatusT[1], "LIGHT", "Light", nullptr);
    IUFillText(&StatusT[2], "COVER_MOTOR", "Cover motor", nullptr);
    IUFillText(&StatusT[3], "FOCUSER", "Focuser", nullptr);
    IUFillTextVector(&StatusTP, StatusT, 4, getDeviceName(), "Status", "Status", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    IUFillText(&FirmwareT[0], "VERSION", "Version", nullptr);
    IUFillTextVector(&FirmwareTP, FirmwareT, 1, getDeviceName(), "Firmware", "Firmware", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    IUFillNumber(&AnglesN[0], "PARK_ANGLE", "Park", "%.0f", MIN_ANGLE, MAX_ANGLE, 5.0, 0.0);
    IUFillNumber(&AnglesN[1], "UNPARK_ANGLE", "Unpark", "%.0f", MIN_ANGLE, MAX_ANGLE, 5.0, 270.0);
    IUFillNumberVector(&AnglesNP, AnglesN, 2, getDeviceName(), "COVER_ANGLES", "Cover Angles", FLATCAP_TAB, IP_RW, 60, IPS_IDLE);

    TemperatureNP[0].fill("TEMPERATURE", "Celsius", "%6.2f", -50, 70., 0., 0.);
    TemperatureNP.fill(getDeviceName(), "FOCUS_TEMPERATURE", "Temperature", MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    TemperatureSettingNP[Calibration].fill("Calibration", "", "%6.2f", -100, 100, 0.5, 0);
    TemperatureSettingNP[Coefficient].fill("Coefficient", "", "%6.2f", -100, 100, 0.5, 0);
    TemperatureSettingNP.fill(getDeviceName(), "T. Settings", "", FOCUSER_TAB, IP_RW, 0, IPS_IDLE);

    TemperatureCompensateSP[INDI_ENABLED].fill("Enable", "", ISS_OFF);
    TemperatureCompensateSP[INDI_DISABLED].fill("Disable", "", ISS_ON);
    TemperatureCompensateSP.fill(getDeviceName(), "T. Compensate", "", FOCUSER_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    FocusRelPosNP[0].setMin(0.);
    FocusRelPosNP[0].setMax(50000.);
    FocusRelPosNP[0].setValue(0);
    FocusRelPosNP[0].setStep(1000);

    FocusAbsPosNP[0].setMin(0.);
    FocusAbsPosNP[0].setMax(100000.);
    FocusAbsPosNP[0].setValue(0);
    FocusAbsPosNP[0].setStep(1000);

    LightIntensityNP[0].setMin(0);
    LightIntensityNP[0].setMax(255);
    LightIntensityNP[0].setStep(5);

    setDriverInterface(AUX_INTERFACE | LIGHTBOX_INTERFACE | DUSTCAP_INTERFACE | FOCUSER_INTERFACE);

    addAuxControls();

    setDefaultPollingPeriod(500);
    addDebugControl();
    addConfigurationControl();
    addPollPeriodControl();

    serialConnection = new Connection::Serial(this);
    serialConnection->registerHandshake([&]() { return Handshake(); });
    registerConnection(serialConnection);

    return true;
}

void Focap::ISGetProperties(const char *dev)
{
    INDI::DefaultDevice::ISGetProperties(dev);
    LI::ISGetProperties(dev);
}

bool Focap::updateProperties()
{
    INDI::DefaultDevice::updateProperties();
    FI::updateProperties();
    DI::updateProperties();
    LI::updateProperties();

    if (isConnected())
    {
        defineProperty(&StatusTP);
        defineProperty(&FirmwareTP);
        defineProperty(&AnglesNP);

        defineProperty(TemperatureNP);
        defineProperty(TemperatureSettingNP);
        defineProperty(TemperatureCompensateSP);

        GetFocusParams();
        getStartupData();
    }
    else
    {
        deleteProperty(StatusTP.name);
        deleteProperty(FirmwareTP.name);
        deleteProperty(AnglesNP.name);
        deleteProperty(TemperatureNP.getName());
        deleteProperty(TemperatureSettingNP.getName());
        deleteProperty(TemperatureCompensateSP.getName());
    }

    return true;
}

const char *Focap::getDefaultName()
{
    return "Gastro Focap";
}

bool Focap::Handshake()
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

    syncDriverInfo();

    return Ack();
}

bool Focap::Ack()
{
    bool success = false;

    for (int i = 0; i < 3; i++)
    {
        if (ping())
        {
            success = true;
            break;
        }
        sleep(1);
    }
    return success;
}

bool Focap::readVersion()
{
    char res[RES_LENGTH] = {0};

    if (sendCommand(":GV#", res, 2) == false)
    {
        return false;
    }

    LOGF_INFO("Detected firmware version %c.%c", res[0], res[1]);

    return true;
}

bool Focap::readTemperature()
{
    char res[RES_LENGTH] = {0};

    if (sendCommand(":GT#", res) == false)
    {
        return false;
    }

    uint32_t temp = 0;
    int rc = sscanf(res, "%X", &temp);
    if (rc > 0)
    {
        // Signed hex
        TemperatureNP[0].setValue(static_cast<int16_t>(temp) / 2.0);
    }
    else
    {
        LOGF_ERROR("Unknown error: focuser temperature value (%s)", res);
        return false;
    }

    return true;
}

bool Focap::readTemperatureCoefficient()
{
    char res[RES_LENGTH] = {0};

    if (sendCommand(":GC#", res) == false)
        return false;

    uint8_t coefficient = 0;
    int rc = sscanf(res, "%hhX", &coefficient);
    if (rc > 0)
        // Signed HEX of two digits
        TemperatureSettingNP[1].setValue(static_cast<int8_t>(coefficient) / 2.0);
    else
    {
        LOGF_ERROR("Unknown error: focuser temperature coefficient value (%s)", res);
        return false;
    }

    return true;
}

bool Focap::readPosition()
{
    char res[RES_LENGTH] = {0};

    if (sendCommand(":GP#", res) == false)
        return false;

    int32_t pos;
    int rc = sscanf(res, "%X#", &pos);

    if (rc > 0)
        FocusAbsPosNP[0].setValue(pos);
    else
    {
        // LOGF_ERROR("Unknown error: focuser position value (%s)", res);
        return false;
    }

    return true;
}

bool Focap::isMoving()
{
    char res[RES_LENGTH] = {0};

    if (sendCommand(":GI#", res) == false)
        return false;

    // JM 2020-03-13: 01# and 1# should be both accepted
    if (strstr(res, "1#"))
        return true;
    else if (strstr(res, "0#"))
        return false;

    LOGF_ERROR("Unknown error: isMoving value (%s)", res);
    return false;
}

bool Focap::setTemperatureCalibration(double calibration)
{
    char cmd[RES_LENGTH] = {0};
    uint8_t hex = static_cast<int8_t>(calibration * 2) & 0xFF;
    snprintf(cmd, RES_LENGTH, ":PO%02X#", hex);
    return sendCommand(cmd);
}

bool Focap::setTemperatureCoefficient(double coefficient)
{
    char cmd[RES_LENGTH] = {0};
    uint8_t hex = static_cast<int8_t>(coefficient * 2) & 0xFF;
    snprintf(cmd, RES_LENGTH, ":SC%02X#", hex);
    return sendCommand(cmd);
}

bool Focap::SyncFocuser(uint32_t ticks)
{
    char cmd[RES_LENGTH] = {0};
    snprintf(cmd, RES_LENGTH, ":SP%04X#", ticks);
    return sendCommand(cmd);
}

bool Focap::MoveFocuser(uint32_t position)
{
    char cmd[RES_LENGTH] = {0};
    snprintf(cmd, RES_LENGTH, ":SN%04X#", position);
    // Set Position First
    if (sendCommand(cmd) == false)
    {
        return false;
    }
    // Now start motion toward position
    if (sendCommand(":FG#") == false)
    {
        return false;
    }

    return true;
}

bool Focap::setTemperatureCompensation(bool enable)
{
    char cmd[RES_LENGTH] = {0};
    snprintf(cmd, RES_LENGTH, ":%c#", enable ? '+' : '-');
    return sendCommand(cmd);
}

bool Focap::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (strcmp(name, "ANGLES") == 0)
        {
            for (int i = 0; i < n; i++)
            {
                if (strcmp(names[i], "PARK_ANGLE") == 0)
                {
                    setParkAngle((uint16_t)round(values[i]));
                }
                else if (strcmp(names[i], "UNPARK_ANGLE") == 0)
                {
                    setUnparkAngle((uint16_t)round(values[i]));
                }
            }
            return true;
        }
        if (LI::processNumber(dev, name, values, names, n))
        {
            return true;
        }
        if (FI::processNumber(dev, name, values, names, n))
        {
            return true;
        }
        if (TemperatureSettingNP.isNameMatch(name))
        {
            TemperatureSettingNP.update(values, names, n);
            if (!setTemperatureCalibration(TemperatureSettingNP[Calibration].getValue()) ||
                !setTemperatureCoefficient(TemperatureSettingNP[Coefficient].getValue()))
            {
                TemperatureSettingNP.setState(IPS_ALERT);
                TemperatureSettingNP.apply();
                return false;
            }

            TemperatureSettingNP.setState(IPS_OK);
            TemperatureSettingNP.apply();
            return true;
        }
    }
    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

bool Focap::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (LI::processText(dev, name, texts, names, n))
        {
            return true;
        }
    }

    return INDI::DefaultDevice::ISNewText(dev, name, texts, names, n);
}

bool Focap::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (DI::processSwitch(dev, name, states, names, n))
            return true;

        if (LI::processSwitch(dev, name, states, names, n))
            return true;

        if (FI::processSwitch(dev, name, states, names, n))
            return true;

        if (TemperatureCompensateSP.isNameMatch(name))
        {
            int last_index = TemperatureCompensateSP.findOnSwitchIndex();
            TemperatureCompensateSP.update(states, names, n);

            bool rc = setTemperatureCompensation((TemperatureCompensateSP[INDI_ENABLED].getState() == ISS_ON));

            if (!rc)
            {
                TemperatureCompensateSP.setState(IPS_ALERT);
                TemperatureCompensateSP.reset();
                TemperatureCompensateSP[last_index].setState(ISS_ON);
                TemperatureCompensateSP.apply();
                return false;
            }

            TemperatureCompensateSP.setState(IPS_OK);
            TemperatureCompensateSP.apply();
            return true;
        }
    }

    return INDI::DefaultDevice::ISNewSwitch(dev, name, states, names, n);
}

void Focap::GetFocusParams()
{
    if (readPosition())
    {
        FocusAbsPosNP.apply();
    }
    if (readTemperature())
    {
        TemperatureNP.apply();
    }
    if (readTemperatureCoefficient())
    {
        TemperatureSettingNP.apply();
    }
}
/*
IPState Focap::MoveFocuser(FocusDirection dir, int speed, uint16_t duration)
{
    // either go all the way in or all the way out
    // then use timer to stop
    if (dir == FOCUS_INWARD)
        MoveFocuser(0);
    else
        MoveFocuser(static_cast<uint32_t>(FocusMaxPosNP[0].getValue()));

    IEAddTimer(duration, &Focap::timedMoveHelper, this);
    return IPS_BUSY;
}
*/
void Focap::timedMoveHelper(void *context)
{
    static_cast<Focap *>(context)->timedMoveCallback();
}

void Focap::timedMoveCallback()
{
    AbortFocuser();
    FocusAbsPosNP.setState(IPS_IDLE);
    FocusRelPosNP.setState(IPS_IDLE);
    FocusTimerNP.setState(IPS_IDLE);
    FocusTimerNP[0].setValue(0);
    FocusAbsPosNP.apply();
    FocusRelPosNP.apply();
    FocusTimerNP.apply();
}

IPState Focap::MoveAbsFocuser(uint32_t targetTicks)
{
    targetPos = targetTicks;

    if (!MoveFocuser(targetPos))
    {
        return IPS_ALERT;
    }

    return IPS_BUSY;
}

IPState Focap::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    // Clamp
    int32_t offset = ((dir == FOCUS_INWARD) ? -1 : 1) * static_cast<int32_t>(ticks);
    int32_t newPosition = FocusAbsPosNP[0].getValue() + offset;
    newPosition = std::max(static_cast<int32_t>(FocusAbsPosNP[0].getMin()), std::min(static_cast<int32_t>(FocusAbsPosNP[0].getMax()), newPosition));

    if (!MoveFocuser(newPosition))
        return IPS_ALERT;

    FocusRelPosNP[0].setValue(ticks);
    FocusRelPosNP.setState(IPS_BUSY);

    return IPS_BUSY;
}

bool Focap::ISSnoopDevice(XMLEle *root)
{
    LI::snoop(root);

    return INDI::DefaultDevice::ISSnoopDevice(root);
}

bool Focap::saveConfigItems(FILE *fp)
{
    INDI::DefaultDevice::saveConfigItems(fp);

    return LI::saveConfigItems(fp) && FI::saveConfigItems(fp);
}

bool Focap::ping()
{
    char response[FLAT_RES] = {0};

    if (!sendCommand(">P000", response))
        return false;

    char productString[3] = {0};
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

bool Focap::getStartupData()
{
    bool rc1 = getFirmwareVersion();
    bool rc2 = getStatus();
    bool rc3 = getBrightness();
    bool rc4 = getParkAngle();
    bool rc5 = getUnparkAngle();

    return (rc1 && rc2 && rc3 && rc4 && rc5);
}

IPState Focap::ParkCap()
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

IPState Focap::UnParkCap()
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

bool Focap::setParkAngle(uint16_t value)
{
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

    char angleString[4] = {0};
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

bool Focap::setUnparkAngle(uint16_t value)
{
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

    char angleString[4] = {0};
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

bool Focap::getParkAngle()
{
    if (isSimulation())
    {
        return true;
    }

    char response[FLAT_RES] = {0};
    if (!sendCommand(">K000", response))
        return false;

    char angleString[4] = {0};
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

bool Focap::getUnparkAngle()
{
    if (isSimulation())
    {
        return true;
    }

    char response[FLAT_RES] = {0};
    if (!sendCommand(">H000", response))
        return false;

    char angleString[4] = {0};
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

bool Focap::EnableLightBox(bool enable)
{
    char command[FLAT_CMD];
    char response[FLAT_RES];

    if (ParkCapSP[1].getState() == ISS_ON)
    {
        LOG_ERROR("Cannot control light while cap is unparked.");
        return false;
    }

    if (isSimulation())
    {
        return true;
    }

    if (enable)
    {
        strncpy(command, ">L000", FLAT_CMD);
    }
    else
    {
        strncpy(command, ">D000", FLAT_CMD);
    }

    if (!sendCommand(command, response))
    {
        return false;
    }

    char expectedResponse[FLAT_RES];
    if (enable)
    {
        snprintf(expectedResponse, FLAT_RES, "*L%02d000", productID);
    }
    else
    {
        snprintf(expectedResponse, FLAT_RES, "*D%02d000", productID);
    }

    return (strstr(response, expectedResponse));
}

bool Focap::getStatus()
{
    char response[FLAT_RES];

    if (isSimulation())
    {
        if (ParkCapSP.getState() == IPS_BUSY && --simulationWorkCounter <= 0)
        {
            ParkCapSP.setState(IPS_OK);
            ParkCapSP.apply();
            simulationWorkCounter = 0;
        }

        if (ParkCapSP.getState() == IPS_BUSY)
        {
            response[4] = '1';
            response[6] = '0';
        }
        else
        {
            response[4] = '0';
            // Parked/Closed
            if (ParkCapSP[CAP_PARK].getState() == ISS_ON)
            {
                response[6] = '1';
            }
            else
            {
                response[6] = '2';
            }
        }

        response[5] = (LightSP[FLAT_LIGHT_ON].getState() == ISS_ON) ? '1' : '0';
    }
    else
    {
        if (!sendCommand(">S000", response))
        {
            return false;
        }
    }

    char flatcapStatus = *(response + 4) - '0';
    char lightStatus = *(response + 5) - '0';
    char coverStatus = *(response + 6) - '0';
    bool focuserStatus = isMoving();

    bool statusUpdated = false;

    if (focuserStatus != prevFocuserStatus)
    {
        prevFocuserStatus = focuserStatus;

        statusUpdated = true;

        if (focuserStatus)
        {
            IUSaveText(&StatusT[3], "Moving");
        }
        else
        {
            IUSaveText(&StatusT[3], "Stopped");
        }
    }

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
            if (ParkCapSP.getState() == IPS_BUSY || ParkCapSP.getState() == IPS_IDLE)
            {
                ParkCapSP.reset();
                ParkCapSP[0].setState(ISS_ON);
                ParkCapSP.setState(IPS_OK);
                LOG_INFO("Cover closed.");
                ParkCapSP.apply();
            }
            break;

        case 2:
            IUSaveText(&StatusT[0], "Open");
            if (ParkCapSP.getState() == IPS_BUSY || ParkCapSP.getState() == IPS_IDLE)
            {
                ParkCapSP.reset();
                ParkCapSP[1].setState(ISS_ON);
                ParkCapSP.setState(IPS_OK);
                LOG_INFO("Cover open.");
                ParkCapSP.apply();
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
            LightSP[1].setState(ISS_ON);
            LightSP[0].setState(ISS_OFF);
            LightSP.apply();
            break;

        case 1:
            IUSaveText(&StatusT[1], "On");
            LightSP[0].setState(ISS_ON);
            LightSP[1].setState(ISS_OFF);
            LightSP.apply();
            break;
        }
    }

    if (flatcapStatus != prevFlatcapStatus)
    {
        prevFlatcapStatus = flatcapStatus;

        statusUpdated = true;

        switch (flatcapStatus)
        {
        case 0:
            IUSaveText(&StatusT[2], "Stopped");
            break;

        case 1:
            IUSaveText(&StatusT[2], "Moving");
            break;
        }
    }

    if (statusUpdated)
    {
        IDSetText(&StatusTP, nullptr);
    }

    return true;
}

bool Focap::getFirmwareVersion()
{
    if (isSimulation())
    {
        IUSaveText(&FirmwareT[0], "Simulation");
        IDSetText(&FirmwareTP, nullptr);
        return true;
    }

    char response[FLAT_RES] = {0};
    if (!sendCommand(">V000", response))
    {
        return false;
    }

    char versionString[4] = {0};
    snprintf(versionString, 4, "%s", response + 4);
    IUSaveText(&FirmwareT[0], versionString);
    IDSetText(&FirmwareTP, nullptr);

    return true;
}

void Focap::TimerHit()
{
    if (!isConnected())
    {
        return;
    }

    getStatus();

    // parking or unparking timed out, try again
    if (ParkCapSP.getState() == IPS_BUSY && !strcmp(StatusT[0].text, "Timed out"))
    {
        if (ParkCapSP[0].getState() == ISS_ON)
            ParkCap();
        else
            UnParkCap();
    }

    bool rc = readPosition();
    if (rc)
    {
        if (fabs(lastPos - FocusAbsPosNP[0].getValue()) > 5)
        {
            FocusAbsPosNP.apply();
            lastPos = static_cast<uint32_t>(FocusAbsPosNP[0].getValue());
        }
    }

    rc = readTemperature();
    if (rc)
    {
        if (std::abs(lastTemperature - TemperatureNP[0].getValue()) >= 0.5)
        {
            TemperatureNP.apply();
            lastTemperature = static_cast<uint32_t>(TemperatureNP[0].getValue());
        }
    }

    if (FocusAbsPosNP.getState() == IPS_BUSY || FocusRelPosNP.getState() == IPS_BUSY)
    {
        if (!isMoving())
        {
            FocusAbsPosNP.setState(IPS_OK);
            FocusRelPosNP.setState(IPS_OK);
            FocusAbsPosNP.apply();
            FocusRelPosNP.apply();
            lastPos = static_cast<uint32_t>(FocusAbsPosNP[0].getValue());
            LOG_INFO("Focuser reached requested position.");
        }
    }

    SetTimer(getCurrentPollingPeriod());
}

bool Focap::getBrightness()
{
    if (isSimulation())
    {
        return true;
    }

    char response[FLAT_RES] = {0};
    if (!sendCommand(">J000", response))
    {
        return false;
    }

    char brightnessString[4] = {0};
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
        LightIntensityNP[0].setValue(brightnessValue);
        LightIntensityNP.apply();
    }

    return true;
}

bool Focap::SetLightBoxBrightness(uint16_t value)
{
    if (isSimulation())
    {
        LightIntensityNP[0].setValue(value);
        LightIntensityNP.apply();
        return true;
    }

    char command[FLAT_CMD] = {0};
    char response[FLAT_RES] = {0};

    snprintf(command, FLAT_CMD, ">B%03d", value);

    if (!sendCommand(command, response))
    {
        return false;
    }

    char brightnessString[4] = {0};
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
        LightIntensityNP[0].setValue(brightnessValue);
        LightIntensityNP.apply();
    }

    return true;
}

bool Focap::AbortFocuser()
{
    return sendCommand(":FQ#");
}

bool Focap::sendCommand(const char *command, char *response, int nret)
{
    if (isSimulation())
    {
        return true;
    }
    if (command[0] == '>')
    {
        int nbytes_read = 0, nbytes_written = 0, rc = -1;
        char buffer[FLAT_CMD + 1] = {0};
        char errstr[MAXRBUF] = {0};
        int timeout = (command[1] == 'O' || command[1] == 'C' || command[1] == 'Z' || command[1] == 'A') ? FLAT_MOTOR_TIMEOUT : FLAT_TIMEOUT;
        tcflush(PortFD, TCIOFLUSH);

        snprintf(buffer, FLAT_CMD + 1, "%s#", command);
        if ((rc = tty_write(PortFD, buffer, FLAT_CMD, &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(rc, errstr, MAXRBUF);
            LOGF_ERROR("%s write error: %s", command, errstr);
            return false;
        }
        if ((rc = tty_nread_section(PortFD, response, FLAT_RES + 1, '#', timeout, &nbytes_read)) != TTY_OK)
        {
            tty_error_msg(rc, errstr, MAXRBUF);
            LOGF_ERROR("%s read error: %s", command, errstr);
            return false;
        }
        response[nbytes_read - 1] = 0;
        tcflush(PortFD, TCIOFLUSH);
        return true;
    }
    else if (command[0] == ':')
    {
        int nbytes_written = 0, nbytes_read = 0, rc = -1;
        tcflush(PortFD, TCIOFLUSH);
        LOGF_DEBUG("CMD <%s>", command);

        if ((rc = tty_write_string(PortFD, command, &nbytes_written)) != TTY_OK)
        {
            char errstr[MAXRBUF] = {0};
            tty_error_msg(rc, errstr, MAXRBUF);
            LOGF_ERROR("Serial write error: %s.", errstr);
            return false;
        }

        if (response == nullptr)
        {
            tcdrain(PortFD);
            return true;
        }

        // this is to handle the GV command which doesn't return the terminator, use the number of chars expected
        if (nret == 0)
        {
            rc = tty_nread_section(PortFD, response, RES_LENGTH, '#', ML_TIMEOUT, &nbytes_read);
        }
        else
        {
            rc = tty_read(PortFD, response, nret, ML_TIMEOUT, &nbytes_read);
        }
        if (rc != TTY_OK)
        {
            char errstr[MAXRBUF] = {0};
            tty_error_msg(rc, errstr, MAXRBUF);
            LOGF_ERROR("Serial read error: %s.", errstr);
            return false;
        }

        LOGF_DEBUG("RES %s", response);

        tcflush(PortFD, TCIOFLUSH);

        return true;
    }
    else if (command[0] != ':' && command[0] != '>')
    {
        LOGF_ERROR("Command not recognised: %s", command);
        return false;
    }
    return false;
}

void Focap::parkTimeoutHelper(void *context)
{
    static_cast<Focap *>(context)->parkTimeout();
}

void Focap::unparkTimeoutHelper(void *context)
{
    static_cast<Focap *>(context)->unparkTimeout();
}

void Focap::parkTimeout()
{
    if (ParkCapSP.getState() == IPS_BUSY)
    {
        LOG_WARN("Parking cap timed out. Retrying...");
        ParkCap();
    }
}

void Focap::unparkTimeout()
{
    if (ParkCapSP.getState() == IPS_BUSY)
    {
        LOG_WARN("UnParking cap timed out. Retrying...");
        UnParkCap();
    }
}