#pragma once

#include "defaultdevice.h"
#include "indilightboxinterface.h"
#include "indidustcapinterface.h"
#include "indifocuser.h"

#include <stdint.h>
#include <chrono>

namespace Connection
{
class Serial;
}

class FlatCap : public INDI::DefaultDevice, public INDI::LightBoxInterface, public INDI::DustCapInterface, public INDI::Focuser
{
    public:
        FlatCap();
        virtual ~FlatCap() = default;

        virtual bool initProperties() override;
        virtual void ISGetProperties(const char *dev) override;
        virtual bool updateProperties() override;

        virtual bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;
        virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
        virtual bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;
        virtual bool ISSnoopDevice(XMLEle *root) override;

        static void parkTimeoutHelper(void *context);
        static void unparkTimeoutHelper(void *context);

        static void timedMoveHelper(void * context);

    protected:
        const char *getDefaultName() override;

        virtual bool Handshake() override;

        // virtual IPState MoveFocuser(FocusDirection dir, int speed, uint16_t duration) override;
        virtual IPState MoveAbsFocuser(uint32_t targetTicks) override;
        virtual IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks) override;
        virtual bool SyncFocuser(uint32_t ticks) override;

        virtual bool AbortFocuser() override;
        virtual void TimerHit() override;
        virtual bool saveConfigItems(FILE * fp) override;

        virtual bool saveConfigItems(FILE *fp) override;
        void TimerHit() override;

        virtual IPState ParkCap() override;
        virtual IPState UnParkCap() override;

        virtual bool SetLightBoxBrightness(uint16_t value) override;
        virtual bool EnableLightBox(bool enable) override;

    private:
        bool getStartupData();
        bool ping();
        bool getStatus();
        bool getFirmwareVersion();
        bool getBrightness();
        bool getParkAngle();
        bool getUnparkAngle();
        bool setParkAngle(uint16_t value);
        bool setUnparkAngle(uint16_t value);

        void parkTimeout();
        int parkTimeoutID { -1 };
        void unparkTimeout();
        int unparkTimeoutID { -1 };

        ITextVectorProperty StatusTP;
        IText StatusT[3] {};

        ITextVectorProperty FirmwareTP;
        IText FirmwareT[1] {};

        INumber AnglesN[2];
        INumberVectorProperty AnglesNP;

        int PortFD{ -1 };
        uint16_t productID{ 0 };

        uint8_t simulationWorkCounter{ 0 };
        uint8_t prevCoverStatus{ 0xFF };
        uint8_t prevLightStatus{ 0xFF };
        uint8_t prevMotorStatus{ 0xFF };
        uint16_t prevBrightness{ 0xFF };
        uint16_t prevParkAngle{ 0 };
        uint16_t prevUnparkAngle{ 0 };

        Connection::Serial *serialConnection{ nullptr };

        bool Ack();
        bool sendCommand(const char* cmd, char* res = nullptr, bool silent = false, int nret = 0);

        void GetFocusParams();
        bool readTemperature();
		bool readTemperatureCoefficient();
        bool readPosition();
        bool readVersion();
        bool isMoving();

        bool MoveFocuser(uint32_t position);
        bool setTemperatureCalibration(double calibration);
        bool setTemperatureCoefficient(double coefficient);
        bool setTemperatureCompensation(bool enable);
        void timedMoveCallback();

        uint32_t targetPos { 0 }, lastPos { 0 }, lastTemperature { 0 };

        INDI::PropertyNumber TemperatureNP {1};

        INDI::PropertyNumber TemperatureSettingNP {2};
        enum
        {
            Calibration,
            Coefficient
        };

        INDI::PropertySwitch TemperatureCompensateSP {2};

        static const uint8_t RES_LENGTH { 32 };
        static const uint8_t ML_TIMEOUT { 3 };
};