#pragma once

#include "qemumodule.h"

class Esp8266Gpio;

class Esp8266Rtc : public QemuModule {
public:
    Esp8266Rtc( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                uint64_t memStart, uint64_t memEnd, Esp8266Gpio* gpio );

    void reset() override;

protected:
    void readRegister() override;
    void writeRegister() override;

private:
    uint32_t m_regs[0x40];
    Esp8266Gpio* m_gpio;
};
