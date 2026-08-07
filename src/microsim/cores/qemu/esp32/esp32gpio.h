/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "e-element.h"
#include "esp32pin.h"
#include "qemumodule.h"

class Esp32Gpio : public QemuModule, public eElement {
    friend class Esp32;
    friend class Esp32s3;
    friend class Esp32c3;

public:
    Esp32Gpio( QemuDevice* mcu, QString name, int n, uint32_t* clk = nullptr, uint64_t memStart = 0,
               uint64_t memEnd = 0, int nPins = 40, int in1Base = 33 );
    ~Esp32Gpio();

    void reset() override;

    uint32_t readPort( int in );

    void writeIoMuxReg( uint8_t pin, uint16_t value );

    static int gpioFromId( const QString& id );

    //Esp32Pin* getPin( int i ) { return m_pins.at(i); }
    //uint size(){ return m_pins.size(); }

protected:
    void writeRegister() override;
    void readRegister() override;

    Esp32Pin* createPin( int i, QString id, QemuDevice* mcu );

    void createIoMux();

    //void cofigPort( uint32_t config, uint8_t shift );
    //void setPortState( uint16_t state );
    void matrixInChanged( int func );
    void matrixOutChanged( int pin );
    void setGpioState( uint32_t newState );
    void setGpioDir( uint32_t newEnable );
    void setGpioState1( uint32_t newState );
    void setGpioDir1( uint32_t newEnable );
    void clearStatus( int i );

    std::vector<Esp32Pin*> m_pins;
    std::vector<Esp32Pin*> m_espPad;
    Esp32Pin* m_dummyPin;

    int m_nPins;
    int m_in1Base;

    uint16_t m_pinState;

    uint32_t m_gpioState;
    uint32_t m_gpioEnable;
    uint32_t m_strapMode;
    uint32_t m_gpioState1;
    uint32_t m_gpioEnable1;
    uint32_t m_gpioStatus[2];

    //uint32_t gpio_in[2];

    //uint32_t gpio_pcpu_int[2];
    //uint32_t gpio_acpu_int[2];

    // uint32_t m_gpioPin[40]; // Managed by Pin

    //uint32_t m_gpioInFunc[256];
    //uint32_t m_gpioOutFunc[256];

    funcPin m_matrixIn[256]; // Matrix created in Esp32::createMatrix()
    funcPin m_matrixOut[256]; // Matrix created in Esp32::createMatrix()
};
