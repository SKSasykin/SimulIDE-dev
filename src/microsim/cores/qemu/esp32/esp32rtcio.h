#pragma once

#include "qemumodule.h"

class Esp32Gpio;

class Esp32RtcIo : public QemuModule {
public:
    Esp32RtcIo( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                uint64_t memStart, uint64_t memEnd, Esp32Gpio* gpio );

    void reset() override;

protected:
    void readRegister() override;
    void writeRegister() override;

private:
    uint32_t m_regs[0x100];
    Esp32Gpio* m_gpio;
};
