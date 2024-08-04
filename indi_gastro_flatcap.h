#pragma once

#include "defaultdevice.h"
#include "indilightboxinterface.h"
#include "indidustcapinterface.h"

#include <stdint.h>

namespace Connection
{
class Serial;
}

class FlatCap : public INDI::DefaultDevice, public INDI::LightBoxInterface, public INDI::DustCapInterface
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

    protected:
        const char *getDefaultName() override;

        virtual bool saveConfigItems(FILE *fp) override;
        void TimerHit() override;

        // From Dust Cap
        virtual IPState ParkCap() override;
        virtual IPState UnParkCap() override;

        // From Light Box
        virtual bool SetLightBoxBrightness(uint16_t value) override;
        virtual bool EnableLightBox(bool enable) override;

    private:
        bool sendCommand(const char *command, char *response);
        bool getStartupData();
        bool ping();
        bool getStatus();
        bool getFirmwareVersion();
        bool getBrightness();

        void parkTimeout();
        int parkTimeoutID { -1 };
        void unparkTimeout();
        int unparkTimeoutID { -1 };

        bool Handshake();

        // Status
        ITextVectorProperty StatusTP;
        IText StatusT[3] {};

        // Firmware version
        ITextVectorProperty FirmwareTP;
        IText FirmwareT[1] {};

        int PortFD{ -1 };
        uint16_t productID{ 0 };

        uint8_t simulationWorkCounter{ 0 };
        uint8_t prevCoverStatus{ 0xFF };
        uint8_t prevLightStatus{ 0xFF };
        uint8_t prevMotorStatus{ 0xFF };
        uint8_t prevBrightness{ 0xFF };

        Connection::Serial *serialConnection{ nullptr };
};