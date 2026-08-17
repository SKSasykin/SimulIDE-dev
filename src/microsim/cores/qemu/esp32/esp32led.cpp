/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QDebug>

#include "esp32led.h"
#include "iopin.h"
#include "qemudevice.h"
#include "simulator.h"

Esp32Led::Esp32Led( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
                    LedcVariant variant, int nChannels, int nTimers )
    : QemuModule( mcu, name, n, clk, memStart, memEnd )
    , m_variant( variant )
    , m_nChannels( nChannels )
    , m_nTimers( nTimers ) {
    for ( int i = 0; i < m_nTimers; ++i )
        m_timers[i] = new LedTimer( name + "-Timer" + QString::number( i ) );

    for ( int i = 0; i < m_nChannels; ++i )
        m_leds[i] = new LedPwm( name + "-Led" + QString::number( i ) );
}
Esp32Led::~Esp32Led() { }

void Esp32Led::reset() {
    for ( int i = 0; i < m_nTimers; ++i )
        m_timers[i]->m_leds.clear();

    for ( int i = 0; i < m_nChannels; ++i ) {
        m_leds[i]->m_duty = 0;
        m_leds[i]->m_enabled = false;
        m_leds[i]->m_idleLevel = false;
        m_leds[i]->setOutput( false );
        m_leds[i]->setTimer( m_timers[0] );
    }
}

int Esp32Led::channelFromOffset( uint64_t offset ) {
    if ( m_variant == LedcVariant::Esp32 ) {
        if ( offset < 0x0A0 ) { // HS block: 8 channels, stride 0x14
            int ch = offset / 0x14;
            return ( ch < 8 ) ? ch : -1;
        }
        if ( offset < 0x140 ) {
            int ch = 8 + ( offset - 0x0A0 ) / 0x14;
            return ( ch < 16 ) ? ch : -1;
        }
        return -1;
    }
    // ESP32-S3 / ESP32-C3: single block of low-speed channels at 0x000
    if ( offset >= (uint64_t)m_nChannels * 0x14 )
        return -1;
    return offset / 0x14;
}

int Esp32Led::timerFromOffset( uint64_t offset ) {
    if ( m_variant == LedcVariant::Esp32 ) {
        if ( offset >= 0x140 && offset < 0x160 )
            return ( offset - 0x140 ) / 8;
        if ( offset >= 0x160 && offset < 0x180 )
            return 4 + ( offset - 0x160 ) / 8;
        return -1;
    }
    // ESP32-S3 / ESP32-C3: timers at 0x0A0
    if ( offset >= 0x0A0 && offset < 0x0C0 )
        return ( offset - 0x0A0 ) / 8;
    return -1;
}

void Esp32Led::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;

    int ch = channelFromOffset( offset );
    if ( ch >= 0 ) {
        uint64_t base = ( m_variant == LedcVariant::Esp32 && offset >= 0x0A0 ) ? 0x0A0 : 0x000;
        uint64_t sub = ( offset - base ) % 0x14;

        switch ( sub ) {
        case 0x000: { // CHx_CONF0
            int timerSel = m_eventValue & 0b11;
            bool sigOutEn = ( m_eventValue >> 2 ) & 1;
            bool idleLv = ( m_eventValue >> 3 ) & 1;

            int timer = timerSel;
            if ( m_variant == LedcVariant::Esp32 && ch >= 8 ) // LS channels use LSTIMER0-3
                timer = 4 + timerSel;

            m_leds[ch]->setTimer( m_timers[timer] );
            m_leds[ch]->m_enabled = sigOutEn;
            m_leds[ch]->m_idleLevel = idleLv;

            if ( !sigOutEn )
                m_leds[ch]->setOutput( idleLv );
            else
                m_leds[ch]->ovf( m_timers[timer]->m_period );
        } break;
        case 0x004: // HPOINT (ignored: phase offset)
            break;
        case 0x008: { // CHx_DUTY
            m_leds[ch]->m_duty = m_eventValue;
            if ( m_leds[ch]->m_enabled && m_leds[ch]->m_timer )
                m_leds[ch]->ovf( m_leds[ch]->m_timer->m_period );
        } break;
        case 0x00C: // CHx_CONF1 (duty fade, ignored)
            break;
        default: // 0x010 DUTY_R (read only)
            break;
        }
        return;
    }

    int t = timerFromOffset( offset );
    if ( t >= 0 && ( offset % 8 ) == 0 ) { // TIMERx_CONF
        uint32_t val = m_eventValue;
        uint32_t div, dutyRes;
        bool pause, rst;

        if ( m_variant == LedcVariant::Esp32 ) {
            dutyRes = val & 0x1F;          // [4:0]
            div = ( val >> 5 ) & 0x3FFFF;  // [22:5]
            pause = ( val >> 23 ) & 1;
            rst = ( val >> 24 ) & 1;
        } else { // S3 / C3
            dutyRes = val & 0xF;           // [3:0]
            div = ( val >> 4 ) & 0x3FFFF;  // [21:4]
            pause = ( val >> 22 ) & 1;
            rst = ( val >> 23 ) & 1;
        }

        m_timers[t]->m_dutyRes = dutyRes;

        if ( pause || rst || div == 0 ) {
            m_timers[t]->setPeriod( 0 );
        } else {
            uint32_t clk = m_frequency ? *m_frequency : ( m_variant == LedcVariant::Esp32 ? 80000000 : 40000000 );
            if ( !clk )
                clk = m_variant == LedcVariant::Esp32 ? 80000000 : 40000000;
            // f_pwm = clk*256 / (clock_divider * 2^duty_res)
            long double ps = (long double)( 1ULL << dutyRes ) * div * 1e12L / ( (long double)clk * 256.0L );
            m_timers[t]->setPeriod( (uint64_t)ps );
        }
    }
}

void Esp32Led::readRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t value = 0;

    int ch = channelFromOffset( offset );
    if ( ch >= 0 ) {
        uint64_t base = ( m_variant == LedcVariant::Esp32 && offset >= 0x0A0 ) ? 0x0A0 : 0x000;
        uint64_t sub = ( offset - base ) % 0x14;
        if ( sub == 0x010 ) // CHx_DUTY_R
            value = m_leds[ch]->m_duty;
    }

    m_arena->regData = value;
    m_arena->qemuAction = SIM_READ;
}

IoPin** Esp32Led::getPinPtr( int n ) {
    if ( n >= m_nChannels )
        return nullptr;
    return &m_leds[n]->m_pin;
}

void Esp32Led::setDummy( IoPin* pin ) {
    for ( int i = 0; i < m_nChannels; ++i )
        m_leds[i]->m_pin = pin;
}

// -------------------------------------------------

LedPwm::LedPwm( QString id ) : eElement( id ) {
    m_timer = nullptr;
}
LedPwm::~LedPwm() { }

void LedPwm::initialize() {
    m_duty = 0;
    m_enabled = false;
    m_idleLevel = false;
    m_timer = nullptr;
}

void LedPwm::scheduleEvents() {
    Simulator::self()->addEvent( m_matchTime, this );
}

void LedPwm::ovf( uint64_t p ) {
    if ( !m_enabled || p == 0 ) {
        Simulator::self()->cancelEvents( this );
        setOutput( m_idleLevel );
        return;
    }

    uint32_t dutyRes = m_timer ? m_timer->m_dutyRes : 0;
    if ( dutyRes >= 32 )
        dutyRes = 0;
    uint64_t ticks = 1ULL << dutyRes; // counter period in clock ticks

    uint64_t high = m_duty >> 4; // DUTY int part = high-phase ticks
    if ( high == 0 ) {           // 0% duty: output stays low
        Simulator::self()->cancelEvents( this );
        setOutput( false );
        return;
    }

    setOutput( true );
    if ( high >= ticks ) {       // 100% duty: stay high until next overflow
        Simulator::self()->cancelEvents( this );
        return;
    }
    m_matchTime = (uint64_t)( (long double)p * high / ticks );
    if ( m_matchTime > 0 )
        scheduleEvents();
}

void LedPwm::runEvent() {
    setOutput( false );
}

void LedPwm::setTimer( LedTimer* t ) {
    if ( m_timer == t )
        return;

    if ( m_timer )
        m_timer->remLedPwm( this );
    m_timer = t;
    m_timer->addLedPwm( this );
}

void LedPwm::setOutput( bool state ) {
    if ( m_pin )
        m_pin->setOutState( state );
}
// -------------------------------------------------

LedTimer::LedTimer( QString id ) : eElement( id ) { }
LedTimer::~LedTimer() { }

void LedTimer::initialize() {
    m_period = 0;
    m_dutyRes = 0;
    m_leds.clear();
}

void LedTimer::runEvent() {
    for ( LedPwm* pwm : m_leds )
        pwm->ovf( m_period );

    if ( m_period )
        Simulator::self()->addEvent( m_period, this );
}

void LedTimer::setPeriod( uint64_t p ) {
    if ( m_period == p )
        return;

    if ( ( m_period == 0 ) || ( p == 0 ) ) // Start or Stop
    {
        if ( p == 0 )
            Simulator::self()->cancelEvents( this ); // Stop
        else
            Simulator::self()->addEvent( p, this ); // Start
        for ( LedPwm* pwm : m_leds )
            pwm->ovf( p );
    }
    m_period = p;
}

uint32_t LedTimer::readCounter() {
    return 0;
}

void LedTimer::addLedPwm( LedPwm* l ) {
    if ( !m_leds.contains( l ) )
        m_leds.append( l );
}

void LedTimer::remLedPwm( LedPwm* l ) {
    m_leds.removeOne( l );
}