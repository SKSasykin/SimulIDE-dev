/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 *   ( see copyright.txt file at root folder )                             *
 ***************************************************************************/

#include "esp8266adc.h"

#include "iopin.h"

#define SAR_CTRL_REG       0x50
#define SAR_RESULT_REG     0x80
#define SAR_RESULT_COUNT   8
#define SAR_START          ( 1 << 1 )
#define SAR_STATE_MASK     ( 7 << 24 )

Esp8266Adc::Esp8266Adc( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                        uint64_t memStart, uint64_t memEnd, IoPin* pin )
    : QemuModule( mcu, name, n, clk, memStart, memEnd )
    , m_pin( pin )
{
}

Esp8266Adc::~Esp8266Adc()
{
}

uint16_t Esp8266Adc::decodeSample( uint16_t encoded ) const
{
    uint16_t inverted = ~encoded;
    int low = ( inverted & 0xFF ) - 21;
    int corrected = low > 0 ? ( low * 279 ) >> 8 : 0;
    if( corrected > 255 ) corrected = 255;
    return ( inverted & 0x700 ) + corrected;
}

void Esp8266Adc::convert()
{
    double voltage = m_pin ? m_pin->getVoltage() : 0.0;
    if( voltage < 0.0 ) voltage = 0.0;
    if( voltage > 1.0 ) voltage = 1.0;

    uint16_t raw = voltage >= 1.0 - 1e-9
                       ? 1023
                       : (uint16_t)( voltage * 1023.0 );
    uint16_t target = raw * 2;

    uint16_t lower = 0;
    uint16_t upper = 0;
    uint16_t lowerValue = 0;
    uint16_t upperValue = 0x7FF;
    for( uint16_t encoded = 0; encoded < 0x800; ++encoded )
    {
        uint16_t value = decodeSample( encoded );
        if( value <= target && value >= lowerValue )
        {
            lower = encoded;
            lowerValue = value;
        }
        if( value >= target && value <= upperValue )
        {
            upper = encoded;
            upperValue = value;
        }
    }

    int upperCount = 0;
    int bestError = 0x7FFFFFFF;
    for( int count = 0; count <= SAR_RESULT_COUNT; ++count )
    {
        int sum = ( SAR_RESULT_COUNT - count ) * lowerValue + count * upperValue;
        int result = ( sum + 8 ) >> 4;
        int error = qAbs( result - raw );
        if( error < bestError )
        {
            upperCount = count;
            bestError = error;
        }
    }

    for( int i = 0; i < SAR_RESULT_COUNT; ++i )
        ( *m_ioMem )[m_memStart + SAR_RESULT_REG + i * 4] = i < upperCount ? upper : lower;

    ( *m_ioMem )[m_memStart + SAR_CTRL_REG] &= ~SAR_STATE_MASK;
}

void Esp8266Adc::writeRegister()
{
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t previous = ( *m_ioMem )[m_eventAddress];
    ( *m_ioMem )[m_eventAddress] = m_eventValue;

    if( offset == SAR_CTRL_REG && !( previous & SAR_START ) && ( m_eventValue & SAR_START ) )
        convert();
}

void Esp8266Adc::readRegister()
{
    m_arena->regData = ( *m_ioMem )[m_eventAddress];
    m_arena->qemuAction = SIM_READ;
}
