/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QDebug>

#include "esp32gpio.h"

Esp32Gpio::Esp32Gpio( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
                      int nPins, int in1Base )
    : QemuModule( mcu, name, n, clk, memStart, memEnd ), eElement( name ) {
    m_nPins = nPins;
    m_in1Base = in1Base;

    m_dummyPin = new Esp32Pin( 0, "dummy", mcu, nullptr );
    m_dummyPin->setVisible( false );

    m_pins.resize( nPins, m_dummyPin ); // 20,24,28,29,30,31=NULL

    m_espPad.assign( nPins, nullptr );
}
Esp32Gpio::~Esp32Gpio() { }

int Esp32Gpio::gpioFromId( const QString& id ) {
    int n = -1;
    int i = id.size() - 1;
    while( i >= 0 && id.at(i).isDigit() ) --i;
    if( i < id.size() - 1 ) {
        bool ok = false;
        n = id.mid( i + 1 ).toInt( &ok );
        if( !ok ) n = -1;
    }
    return n;
}

void Esp32Gpio::reset() {
    m_gpioState = 0;
    m_gpioEnable = 0;
    m_strapMode = 0;
    m_gpioState1 = 0;
    m_gpioEnable1 = 0;
    m_gpioStatus[0] = 0;
    m_gpioStatus[1] = 0;
}

void Esp32Gpio::writeIoMuxReg( uint8_t pin, uint16_t value ) {
    if ( pin >= (uint8_t)m_espPad.size() )
        return;
    Esp32Pin* espPin = m_espPad[pin];
    if ( espPin )
        espPin->writeIoMuxReg( value );
}

void Esp32Gpio::readRegister() {
    //qDebug() <<"Esp32Gpio::readRegister"<< m_name << m_eventAddress << m_eventValue;

    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t val = 0;

    switch ( offset ) {
    case 0x3C:
        val = readPort( 0 );
        break; // GPIO_IN_REG
    case 0x40:
        val = readPort( 1 );
        break; // GPIO_IN1_REG
    case 0x10:
        val = m_gpioState1;
        break; // GPIO_OUT1_REG
    case 0x2C:
        val = m_gpioEnable1;
        break; // GPIO_ENABLE1_REG
    }
    m_arena->regData = val;
    m_arena->qemuAction = SIM_READ;
}

void Esp32Gpio::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;

    if ( offset == 0x04 ) { // GPIO_OUT_REG
        setGpioState( m_eventValue );
    } else if ( offset == 0x08 ) { // GPIO_OUT_W1TS_REG (set bits)
        setGpioState( m_gpioState | m_eventValue );
    } else if ( offset == 0x0C ) { // GPIO_OUT_W1TC_REG (clear bits)
        setGpioState( m_gpioState & ~m_eventValue );
    } else if ( offset == 0x10 ) { // GPIO_OUT1_REG (pins 32-45)
        setGpioState1( m_eventValue );
    } else if ( offset == 0x14 ) { // GPIO_OUT1_W1TS_REG (set bits)
        setGpioState1( m_gpioState1 | m_eventValue );
    } else if ( offset == 0x18 ) { // GPIO_OUT1_W1TC_REG (clear bits)
        setGpioState1( m_gpioState1 & ~m_eventValue );
    } else if ( offset == 0x20 ) { // GPIO_ENABLE_REG
        setGpioDir( m_eventValue );
    } else if ( offset == 0x24 ) { // GPIO_ENABLE_W1TS_REG (set bits)
        setGpioDir( m_gpioEnable | m_eventValue );
    } else if ( offset == 0x28 ) { // GPIO_ENABLE_W1TC_REG (clear bits)
        setGpioDir( m_gpioEnable & ~m_eventValue );
    } else if ( offset == 0x2C ) { // GPIO_ENABLE1_REG (pins 32-45)
        setGpioDir1( m_eventValue );
    } else if ( offset == 0x30 ) { // GPIO_ENABLE1_W1TS_REG (set bits)
        setGpioDir1( m_gpioEnable1 | m_eventValue );
    } else if ( offset == 0x34 ) { // GPIO_ENABLE1_W1TC_REG (clear bits)
        setGpioDir1( m_gpioEnable1 & ~m_eventValue );
    } else if ( offset >= 0x88 ) {
        if ( offset < 0x130 ) { // GPIO_PINXX_REG
            uint64_t pinNumber = ( offset - 0x88 ) / 4;
            if ( pinNumber < (uint64_t)m_espPad.size() ) {
                Esp32Pin* pin = m_espPad[pinNumber];
                if ( pin )
                    pin->writePinReg( m_eventValue );
            }
        } else if ( offset < 0x530 ) { // GPIO_FUNCY_IN_SEL_CFG_REG
            int func = ( offset - 0x130 ) / 4;
            //m_gpioInFunc[func] = m_eventValue;
            matrixInChanged( func );
        } else if ( offset < 0x530 + (uint64_t)m_nPins*4 ) { // GPIO_FUNCX_OUT_SEL_CFG_REG
            int pin = ( offset - 0x530 ) / 4;
            //m_gpioOutFunc[pin] = m_eventValue;
            matrixOutChanged( pin );
        }
    }
}

void Esp32Gpio::matrixInChanged( int func ) {
    int pin = m_eventValue & 0x3F;
    if ( pin >= m_nPins )
        return;

    Esp32Pin* espPin = m_espPad[pin];
    if ( !espPin )
        return;

    funcPin fp = { nullptr, nullptr, "---" };

    if ( func < 256 )
        fp = m_matrixIn[func];
    //else if( func == 256 ) fp = { nullptr, &m_ioPin[pin], ""};

    //qDebug() << "Esp32::matrixInFunc"<< espPin->pinId() << func << fp.label;
    if ( fp.label == "---" )
        return;

    espPin->setMatrixFunc( m_eventValue, fp );
}

void Esp32Gpio::matrixOutChanged( int pin ) {
    if ( pin >= m_nPins )
        return;
    int func = m_eventValue & 0xFF;

    Esp32Pin* espPin = m_espPad[pin];
    if ( !espPin )
        return;

    funcPin fp = { nullptr, nullptr, "---" };

    if ( func < 256 )
        fp = m_matrixOut[func];
    //else if( func == 256 ) fp = { nullptr, &m_ioPin[pin], ""};

    //qDebug() << "Esp32::matrixOutChanged"<< espPin->pinId() << func << fp.label;
    if ( fp.label == "---" )
        return;

    // val & 1<<11: OEN_INV_SEL 1: Invert the output enable signal
    // val & 1<<10: 1: use output enable from bit n of GPIO_ENABLE_REG;
    //              0: use output enable from peripheral. (R/W)
    // val & 1<< 9: 1. Invert the output value

    espPin->setMatrixFunc( m_eventValue, fp );
}

uint32_t Esp32Gpio::readPort( int in ) {
    uint32_t data = 0;
    if ( in == 0 ) {
        for ( uint8_t i = 0; i < 32; ++i ) {
            IoPin* pin = m_espPad[i];
            if ( pin && pin->getInpState() )
                data |= ( 1 << i );
        }
    } else {
        for ( int i = m_in1Base; i < m_nPins; ++i ) {
            IoPin* pin = m_espPad[i];
            if ( pin && pin->getInpState() )
                data |= ( 1 << ( i - m_in1Base ) );
        }
    }
    return data;
}

void Esp32Gpio::setGpioState( uint32_t newState ) {
    if ( m_gpioState == newState )
        return;

    uint32_t changed = m_gpioState ^ newState;

    for ( uint i = 0; i < 32; ++i ) // GPIO pads 34-39 are input-only.
    {
        Esp32Pin* pin = m_espPad[i];
        if ( !pin )
            continue;

        uint32_t mask = 1 << i;
        if ( changed & mask )
            pin->setOutState( newState & mask );
    }

    m_gpioState = newState;
}

void Esp32Gpio::setGpioDir( uint32_t newEnable ) {
    if ( m_gpioEnable == newEnable )
        return;

    uint32_t changed = m_gpioEnable ^ newEnable;

    for ( uint i = 0; i < 32; ++i ) // GPIO pads 34-39 are input-only.
    {
        Esp32Pin* pin = m_espPad[i];
        if ( !pin )
            continue;

        uint32_t mask = 1 << i;
        if ( changed & mask ) {
            if ( newEnable & mask )
                pin->setPinMode( output );
            else
                pin->setPinMode( input );
            //qDebug() << "Esp32Gpio::setGpioDir" << i << newEnable;
            //pin->changeCallBack( this, !newDirec ); // CallBack only on Inputs
        }
    }
    m_gpioEnable = newEnable;
}

void Esp32Gpio::setGpioState1( uint32_t newState ) { // GPIO_OUT1_REG (pins 32-45)
    if ( m_gpioState1 == newState )
        return;

    uint32_t changed = m_gpioState1 ^ newState;

    for ( uint i = 32; i < (uint)m_nPins; ++i ) {
        Esp32Pin* pin = m_espPad[i];
        if ( !pin )
            continue;

        uint32_t mask = 1 << ( i - 32 );
        if ( changed & mask )
            pin->setOutState( newState & mask );
    }

    m_gpioState1 = newState;
}

void Esp32Gpio::setGpioDir1( uint32_t newEnable ) { // GPIO_ENABLE1_REG (pins 32-45)
    if ( m_gpioEnable1 == newEnable )
        return;

    uint32_t changed = m_gpioEnable1 ^ newEnable;

    for ( uint i = 32; i < (uint)m_nPins; ++i ) {
        Esp32Pin* pin = m_espPad[i];
        if ( !pin )
            continue;

        uint32_t mask = 1 << ( i - 32 );
        if ( changed & mask ) {
            if ( newEnable & mask )
                pin->setPinMode( output );
            else
                pin->setPinMode( input );
        }
    }
    m_gpioEnable1 = newEnable;
}

void Esp32Gpio::clearStatus( int i ) {
    // m_eventValue
}

Esp32Pin* Esp32Gpio::createPin( int i, QString id, QemuDevice* mcu ) {
    Esp32Pin* pin = new Esp32Pin( i, id, mcu, m_dummyPin );

    //m_pins.emplace_back( pin );
    m_espPad[i] = pin;

    return pin;
}

#define U0RXD m_matrixIn[14]
#define U1RXD m_matrixIn[17]
#define U2RXD m_matrixIn[198]
//#define I2C0SCL m_matrixIn[29]
//#define I2C0SDA m_matrixIn[30]
//#define I2C1SCL m_matrixIn[95]
//#define I2C1SDA m_matrixIn[96]

#define SPICLK m_matrixOut[0]
#define SPIQ m_matrixOut[1]
#define SPID m_matrixOut[2]
#define SPICS0 m_matrixOut[5]
#define U0TXD m_matrixOut[14]
#define U1TXD m_matrixOut[17]
#define U2TXD m_matrixOut[198]
#define HSPICLK m_matrixOut[8]
#define HSPIQ m_matrixOut[9]
#define HSPID m_matrixOut[10]
#define HSPICS0 m_matrixOut[11]
#define VSPICLK m_matrixOut[63]
#define VSPIQ m_matrixOut[64]
#define VSPID m_matrixOut[65]
#define VSPICS0 m_matrixOut[68]

void Esp32Gpio::createIoMux() {
    funcPin DUMMY = { nullptr, nullptr, "---" };
    funcPin GPIO = {
        nullptr, nullptr,
        "GPIO" }; /// FIXME: pointer to this, same than any peripheral, Gpio shouldn't control pin if it is assigned to other

    if ( m_nPins != 40 ) {
        // ESP32-S3 / ESP32-C3: no default peripheral functions, all pads plain GPIO
        for ( int i = 0; i < m_nPins; ++i ) {
            Esp32Pin* pad = m_espPad[i];
            if ( pad )
                pad->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
        }
        return;
    }

    // 0:  GPIO0    CLK_OUT1 GPIO0  ----      ----      EMAC_TX_CLK
    m_espPad[0]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 1:  U0TXD    CLK_OUT3 GPIO1  ----      ----      EMAC_RXD2
    m_espPad[1]->setIoMuxFuncs( { U0TXD, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 2:  GPIO2    HSPIWP   GPIO2  HS2_DATA0 SD_DATA0  ----
    m_espPad[2]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 3:  U0RXD    CLK_OUT2 GPIO3  ----      ----      ----
    m_espPad[3]->setIoMuxFuncs( { U0RXD, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 4:  GPIO4    HSPIHD   GPIO4  HS2_DATA1 SD_DATA1E MAC_TX_ER
    m_espPad[4]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 5:  GPIO5    VSPICS0  GPIO5  HS1_DATA6 ---- EMAC_RX_CLK
    m_espPad[5]->setIoMuxFuncs( { GPIO, VSPICS0, GPIO, DUMMY, DUMMY, DUMMY } );

    // 6:  SD_CLK   SPICLK   GPIO6  HS1_CLK   U1CTS ----
    m_espPad[6]->setIoMuxFuncs( { DUMMY, SPICLK, GPIO, DUMMY, DUMMY, DUMMY } );

    // 7:  SD_DATA0 SPIQ     GPIO7  HS1_DATA0 U2RTS ----
    m_espPad[7]->setIoMuxFuncs( { DUMMY, SPIQ, GPIO, DUMMY, DUMMY, DUMMY } );

    // 8:  SD_DATA1 SPID     GPIO8  HS1_DATA1 U2CTS    ----
    m_espPad[8]->setIoMuxFuncs( { DUMMY, SPID, GPIO, DUMMY, DUMMY, DUMMY } );

    // 9:  SD_DATA2 SPIHD    GPIO9  HS1_DATA2 U1RXD    ----
    m_espPad[9]->setIoMuxFuncs( { DUMMY, DUMMY, GPIO, DUMMY, U1RXD, DUMMY } );

    // 10: SD_DATA3 SPIWP    GPIO10 HS1_DATA3 U1TXD    ----
    m_espPad[10]->setIoMuxFuncs( { DUMMY, DUMMY, GPIO, DUMMY, U1TXD, DUMMY } );

    // 11: SD_CMD   SPICS0   GPIO11 HS1_CMD   U1RTS    ----
    m_espPad[11]->setIoMuxFuncs( { DUMMY, SPICS0, GPIO, DUMMY, DUMMY, DUMMY } );

    // 12: MTDI     HSPIQ    GPIO12 HS2_DATA2 SD_DATA2 EMAC_TXD3
    m_espPad[12]->setIoMuxFuncs( { HSPIQ, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 13: MTCK     HSPID    GPIO13 HS2_DATA3 SD_DATA3 EMAC_RX_ER
    m_espPad[13]->setIoMuxFuncs( { HSPID, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 14: MTMS     HSPICLK  GPIO14 HS2_CLK   SD_CLK   EMAC_TXD2
    m_espPad[14]->setIoMuxFuncs( { DUMMY, HSPICLK, GPIO, DUMMY, DUMMY, DUMMY } );

    // 15: MTDO     HSPICS0  GPIO15 HS2_CMD   SD_CMD   EMAC_RXD3
    m_espPad[15]->setIoMuxFuncs( { HSPICS0, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 16: GPIO16   ----     GPIO16 HS1_DATA4 U2RXD    EMAC_CLK_OUT
    m_espPad[16]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, U2RXD, DUMMY } );

    // 17: GPIO17   ----     GPIO17 HS1_DATA5 U2TXD    EMAC_CLK_180
    m_espPad[17]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, U2TXD, DUMMY } );

    // 18: GPIO18   VSPICLK  GPIO18 HS1_DATA7 ----     ----
    m_espPad[18]->setIoMuxFuncs( { GPIO, VSPICLK, GPIO, DUMMY, DUMMY, DUMMY } );

    // 19: GPIO19   VSPIQ    GPIO19 U0CTS     ----     EMAC_TXD0
    m_espPad[19]->setIoMuxFuncs( { GPIO, VSPIQ, GPIO, DUMMY, DUMMY, DUMMY } );

    // 20:
    // 21: GPIO21   VSPIHD   GPIO21 ----       ----    EMAC_TX_EN
    m_espPad[21]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 22: GPIO22   VSPIWP   GPIO22 U0RTS      ----    EMAC_TXD1
    m_espPad[22]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 23: GPIO23   VSPID    GPIO23 HS1_STROBE ----    ----
    m_espPad[23]->setIoMuxFuncs( { GPIO, VSPID, GPIO, DUMMY, DUMMY, DUMMY } );

    // 24:
    // 25: GPIO25 ----      GPIO25 ----       ----    EMAC_RXD0
    m_espPad[25]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 26: GPIO26 ----      GPIO26 ----       ----    EMAC_RXD1
    m_espPad[26]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );

    // 27: GPIO27 ----      GPIO27 ----       ----    EMAC_RX_DV
    m_espPad[27]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
    // 28:
    // 29:
    // 30:
    // 31:
    // 32: GPIOx ---- GPIOx ---- ---- ----
    m_espPad[32]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
    // 33: GPIOx ---- GPIOx ---- ---- ----
    m_espPad[33]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
    // 34: GPIOx ---- GPIOx ---- ---- ----
    m_espPad[34]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
    // 35: GPIOx ---- GPIOx ---- ---- ----
    m_espPad[35]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
    // 36: GPIOx ---- GPIOx ---- ---- ----
    m_espPad[36]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
    // 37: GPIOx ---- GPIOx ---- ---- ----
    m_espPad[37]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
    // 38: GPIOx ---- GPIOx ---- ---- ----
    m_espPad[38]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
    // 39: GPIOx ---- GPIOx ---- ---- ----
    m_espPad[39]->setIoMuxFuncs( { GPIO, DUMMY, GPIO, DUMMY, DUMMY, DUMMY } );
}
