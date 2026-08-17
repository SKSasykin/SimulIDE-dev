/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "esp32adc.h"

// SAR ADC register offsets relative to 0x3FF48800 (SENS block), ESP-IDF v5.x layout
#define SENS_SAR_MEAS_START1_REG   0x54
#define SENS_SAR_MEAS_START2_REG   0x94
#define SENS_SAR_START_FORCE_REG   0x2C
#define SENS_SAR_ATTEN1_REG        0x34
#define SENS_SAR_ATTEN2_REG        0x38

// ESP32-S3 SENS register offsets relative to 0x60008800
#define S3_SAR_MEAS1_CTRL2_REG     0x0C
#define S3_SAR_MEAS2_CTRL2_REG     0x30
#define S3_SAR_ATTEN1_REG          0x14
#define S3_SAR_ATTEN2_REG          0x38

// ESP32-C3 APB_SARADC register offsets relative to 0x60040000
#define C3_ONETIME_SAMPLE_REG      0x20
#define C3_DATA1_STATUS_REG        0x2C
#define C3_DATA2_STATUS_REG        0x30
#define C3_INT_RAW_REG             0x44
#define C3_INT_CLR_REG             0x4C

// Fields of SENS_SAR_MEAS_START1/2_REG / S3 sar_meas1/2_ctrl2:
//   bits 0-15  measN_data_sar
//   bit  16    measN_done_sar
//   bit  17    measN_start_sar
//   bit  18    measN_start_force
//   bits 19-30 sarN_en_pad
//   bit  31    sarN_en_pad_force
#define SENS_MEAS_DATA_MASK        0x0000FFFF
#define SENS_MEAS_DONE_SAR         0x00010000
#define SENS_MEAS_START_SAR        0x00020000
#define SENS_SAR_EN_PAD_M          0x0FFF << 19

// Effective measurement range upper limit (V) per attenuation code.
// Values are from each chip's datasheet ADC calibration results table.
static const double esp32AdcFullScale[] = { 0.95, 1.25, 1.75, 2.45 };
static const double s3AdcFullScale[] = { 0.85, 1.10, 1.60, 2.90 };
static const double c3AdcFullScale[] = { 0.75, 1.05, 1.30, 2.50 };

// ADC1 channel -> GPIO (ADC1_CH0..CH7)
static const int adc1ChannelGpio[] = { 36, 37, 38, 39, 32, 33, 34, 35 };
// ADC2 channel -> GPIO (ADC2_CH0..CH9)
static const int adc2ChannelGpio[] = { 4, 0, 2, 15, 13, 12, 14, 27, 25, 26 };

// ESP32-S3: ADC1_CH0..CH9 -> GPIO1..10, ADC2_CH0..CH9 -> GPIO11..20
static const int s3Adc1ChannelGpio[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
static const int s3Adc2ChannelGpio[] = { 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };

// ESP32-C3: ADC1_CH0..CH4 -> GPIO0..4, ADC2_CH0 -> GPIO5
static const int c3Adc1ChannelGpio[] = { 0, 1, 2, 3, 4 };
static const int c3Adc2ChannelGpio[] = { 5 };

Esp32Adc::Esp32Adc( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
                    Esp32Gpio* gpio, Esp32AdcMode mode )
    : QemuModule( mcu, name, n, clk, memStart, memEnd ) {
    m_gpio = gpio;
    m_mode = mode;
    m_meas1Data = 0;
    m_meas2Data = 0;
    m_meas1Done = 0;
    m_meas2Done = 0;
    m_atten1 = 0;
    m_atten2 = 0;

    if ( mode == Esp32AdcS3 ) {
        m_start1Reg = S3_SAR_MEAS1_CTRL2_REG;
        m_start2Reg = S3_SAR_MEAS2_CTRL2_REG;
        m_atten1Reg = S3_SAR_ATTEN1_REG;
        m_atten2Reg = S3_SAR_ATTEN2_REG;
        m_data1Reg = 0;
        m_data2Reg = 0;
        m_adc1Chans = 10;
        m_adc2Chans = 10;
        m_adc1Gpio = s3Adc1ChannelGpio;
        m_adc2Gpio = s3Adc2ChannelGpio;
    } else if ( mode == Esp32AdcC3 ) {
        m_start1Reg = C3_ONETIME_SAMPLE_REG;
        m_start2Reg = C3_ONETIME_SAMPLE_REG;
        m_atten1Reg = 0;
        m_atten2Reg = 0;
        m_data1Reg = C3_DATA1_STATUS_REG;
        m_data2Reg = C3_DATA2_STATUS_REG;
        m_adc1Chans = 5;
        m_adc2Chans = 1;
        m_adc1Gpio = c3Adc1ChannelGpio;
        m_adc2Gpio = c3Adc2ChannelGpio;
    } else {
        m_start1Reg = SENS_SAR_MEAS_START1_REG;
        m_start2Reg = SENS_SAR_MEAS_START2_REG;
        m_atten1Reg = SENS_SAR_ATTEN1_REG;
        m_atten2Reg = SENS_SAR_ATTEN2_REG;
        m_data1Reg = 0;
        m_data2Reg = 0;
        m_adc1Chans = 8;
        m_adc2Chans = 10;
        m_adc1Gpio = adc1ChannelGpio;
        m_adc2Gpio = adc2ChannelGpio;
        ( *m_ioMem )[m_memStart + SENS_SAR_START_FORCE_REG] = 0x0F; // ADC1/2 reset to 12-bit width.
    }
}
Esp32Adc::~Esp32Adc() {}

uint16_t Esp32Adc::getRaw( int gpio, int atten, int bits ) {
    Esp32Pin* pin = m_gpio ? m_gpio->getPad( gpio ) : nullptr;
    double volt = pin ? pin->getVoltage() : 0.0;
    const double* fullScale = esp32AdcFullScale;
    if ( m_mode == Esp32AdcS3 ) fullScale = s3AdcFullScale;
    else if ( m_mode == Esp32AdcC3 ) fullScale = c3AdcFullScale;
    double vfs = fullScale[atten & 0x03];
    uint16_t maxRaw = ( 1 << bits ) - 1;
    if ( volt < 0.0 ) volt = 0.0;
    if ( volt > vfs ) volt = vfs;
    if ( volt >= vfs - 1e-9 ) return maxRaw;
    return (uint16_t)( ( volt / vfs ) * maxRaw );
}

void Esp32Adc::convert( int adcNum ) {
    const int* chanGpio = ( adcNum == 1 ) ? m_adc1Gpio : m_adc2Gpio;
    int nChans = ( adcNum == 1 ) ? m_adc1Chans : m_adc2Chans;
    uint32_t startReg = ( adcNum == 1 ) ? m_start1Reg : m_start2Reg;
    uint32_t measStart = ( *m_ioMem )[m_memStart + startReg];
    uint32_t enPad = measStart & SENS_SAR_EN_PAD_M;
    uint32_t atten = ( adcNum == 1 ) ? m_atten1 : m_atten2;
    int bits = 12;
    if ( m_mode == Esp32AdcEsp32 ) {
        uint32_t widthReg = ( *m_ioMem )[m_memStart + SENS_SAR_START_FORCE_REG];
        bits = 9 + ( ( widthReg >> ( ( adcNum - 1 ) * 2 ) ) & 0x03 );
    }

    uint16_t raw = 0;
    for ( int ch = 0; ch < nChans; ++ch ) {
        if ( !( enPad & ( 1 << ( 19 + ch ) ) ) )
            continue;

        raw = getRaw( chanGpio[ch], ( atten >> ( ch * 2 ) ) & 0x03, bits );
        break;
    }

    // Publish data + done bits back into the start register so
    // adc_ll_rtc_convert_is_done() / adc_ll_rtc_get_convert_value() see them.
    uint32_t regVal = measStart & ~( SENS_MEAS_DATA_MASK | SENS_MEAS_DONE_SAR );
    ( *m_ioMem )[m_memStart + startReg] = regVal | raw | SENS_MEAS_DONE_SAR;
    if ( adcNum == 1 ) {
        m_meas1Data = raw;
        m_meas1Done = 1;
    } else {
        m_meas2Data = raw;
        m_meas2Done = 1;
    }
}

void Esp32Adc::convertC3() {
    uint32_t onetime = ( *m_ioMem )[m_memStart + C3_ONETIME_SAMPLE_REG];
    int unit = ( onetime & 0x40000000 ) ? 1 : 0;
    int channel = ( onetime >> 25 ) & 0x0F;
    int atten = ( onetime >> 23 ) & 0x03;

    const int* chanGpio = ( unit == 0 ) ? m_adc1Gpio : m_adc2Gpio;
    int nChans = ( unit == 0 ) ? m_adc1Chans : m_adc2Chans;

    uint16_t raw = 0;
    if ( channel < nChans ) {
        raw = getRaw( chanGpio[channel], atten );
    }

    uint32_t dataReg = ( unit == 0 ) ? m_data1Reg : m_data2Reg;
    ( *m_ioMem )[m_memStart + dataReg] = raw;

    // Set done bit in INT_RAW (adc1_done bit31 / adc2_done bit30) so the
    // firmware spin loop in adc_hal_convert() terminates.
    uint32_t doneBit = ( unit == 0 ) ? 0x80000000 : 0x40000000;
    ( *m_ioMem )[m_memStart + C3_INT_RAW_REG] |= doneBit;

    if ( unit == 0 ) {
        m_meas1Data = raw;
        m_meas1Done = 1;
    } else {
        m_meas2Data = raw;
        m_meas2Done = 1;
    }
}

void Esp32Adc::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    ( *m_ioMem )[m_eventAddress] = m_eventValue;

    if ( m_mode == Esp32AdcC3 ) {
        if ( offset == C3_ONETIME_SAMPLE_REG ) {
            if ( m_eventValue & 0x20000000 ) // onetime_start
                convertC3();
        } else if ( offset == C3_INT_CLR_REG ) {
            ( *m_ioMem )[m_memStart + C3_INT_RAW_REG] &= ~m_eventValue;
        }
        return;
    }

    switch ( offset ) {
    case SENS_SAR_MEAS_START1_REG:
    case S3_SAR_MEAS1_CTRL2_REG:
        if ( m_eventValue & SENS_MEAS_START_SAR )
            convert( 1 );
        break;
    case SENS_SAR_MEAS_START2_REG:
    case S3_SAR_MEAS2_CTRL2_REG:
        if ( m_eventValue & SENS_MEAS_START_SAR )
            convert( 2 );
        break;
    case SENS_SAR_ATTEN1_REG:
    case S3_SAR_ATTEN1_REG:
        m_atten1 = m_eventValue;
        break;
    case SENS_SAR_ATTEN2_REG:
        m_atten2 = m_eventValue;
        break;
    }
}

void Esp32Adc::readRegister() {
    uint32_t data = ( *m_ioMem )[m_eventAddress];
    m_arena->regData = data;
    m_arena->qemuAction = SIM_READ;
}
