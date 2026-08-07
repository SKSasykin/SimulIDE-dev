/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 *   ( see copyright.txt file at root folder )                             *
 ***************************************************************************/

#include "esp8266gpio.h"

#include "qemudevice.h"

Esp8266Gpio::Esp8266Gpio( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                          uint64_t memStart, uint64_t memEnd, int nPins )
    : QemuModule( mcu, name, n, clk, memStart, memEnd )
    , eElement( mcu->getId()+"-"+name )
    , m_dummyPin( nullptr )
    , m_nPins( nPins )
{
    m_dummyPin = new Esp32Pin( 0, "dummy", mcu, nullptr );
    m_dummyPin->setVisible( false );
    m_pins.resize( nPins, m_dummyPin );
    m_espPad.assign( nPins, nullptr );
}

Esp8266Gpio::~Esp8266Gpio()
{
}

int Esp8266Gpio::gpioFromId( const QString& id )
{
    if( id == "SD0" ) return 7;  // ESP-12 / NodeMCU documentation aliases
    if( id == "SD1" ) return 8;
    if( id == "SD2" ) return 9;
    if( id == "SD3" ) return 10;
    if( id == "CMD" ) return 11;
    if( id == "CS"  ) return 15;

    int n = -1;
    int i = id.size() - 1;
    while( i >= 0 && id.at(i).isDigit() ) --i;
    if( i < id.size() - 1 )
    {
        bool ok = false;
        n = id.mid( i + 1 ).toInt( &ok );
        if( !ok ) n = -1;
    }
    return n;
}

void Esp8266Gpio::reset()
{
    m_gpioState  = 0;
    m_gpioEnable = 0;
    m_strapMode  = 0x12;
}

Esp32Pin* Esp8266Gpio::createPin( int i, QString id, QemuDevice* mcu )
{
    Esp32Pin* pin = new Esp32Pin( i, id, mcu, m_dummyPin );
    m_espPad[i] = pin;
    return pin;
}

uint32_t Esp8266Gpio::readPort()
{
    uint32_t data = 0;
    for( int i=0; i<m_nPins; ++i )
    {
        Esp32Pin* pin = m_espPad[i];
        if( pin && pin->getInpState() ) data |= 1<<i;
    }
    return data;
}

void Esp8266Gpio::setGpioState( uint32_t newState )
{
    if( m_gpioState == newState ) return;
    uint32_t changed = m_gpioState ^ newState;

    for( int i=0; i<m_nPins; ++i )
    {
        uint32_t mask = 1<<i;
        if( changed & mask )
        {
            Esp32Pin* pin = m_espPad[i];
            if( pin ) pin->setOutState( newState & mask );
        }
    }
    m_gpioState = newState;
}

void Esp8266Gpio::setGpioDir( uint32_t dir )
{
    if( m_gpioEnable == dir ) return;
    uint32_t changed = m_gpioEnable ^ dir;

    for( int i=0; i<m_nPins; ++i )
    {
        uint32_t mask = 1<<i;
        if( changed & mask )
        {
            Esp32Pin* pin = m_espPad[i];
            if( pin ) pin->setPinMode( dir & mask ? output : input );
        }
    }
    m_gpioEnable = dir;
}

void Esp8266Gpio::readRegister()
{
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t value  = 0;

    switch( offset )
    {
        case 0x00: value = m_gpioState;              break; // GPIO_OUT
        case 0x0C: value = m_gpioEnable;             break; // GPIO_ENABLE
        case 0x18: value = readPort();               break; // GPIO_IN
        case 0x20: value = m_strapMode & 0x1F;       break; // GPIO_STRAP
        default: break;
    }
    m_arena->regData = value;
    m_arena->qemuAction = SIM_READ;
}

void Esp8266Gpio::writeRegister()
{
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t value  = m_eventValue;

    switch( offset )
    {
        case 0x00: setGpioState( value );                 break; // GPIO_OUT
        case 0x04: setGpioState( m_gpioState | value );   break; // GPIO_OUT_SET
        case 0x08: setGpioState( m_gpioState & ~value );  break; // GPIO_OUT_CLEAR
        case 0x0C: setGpioDir( value );                   break; // GPIO_ENABLE
        case 0x10: setGpioDir( m_gpioEnable | value );    break; // GPIO_ENABLE_SET
        case 0x14: setGpioDir( m_gpioEnable & ~value );   break; // GPIO_ENABLE_CLEAR
        case 0x20: m_strapMode = value & 0x1F;            break; // GPIO_STRAP
        default: break;
    }
}
