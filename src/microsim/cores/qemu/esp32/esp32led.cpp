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
        m_timers[i] = new LedTimer( name + "-Timer" + QString::number( i ), this, i );

    for ( int i = 0; i < m_nChannels; ++i )
        m_leds[i] = new LedPwm( name + "-Led" + QString::number( i ), this, i );
}
Esp32Led::~Esp32Led() { }

void Esp32Led::reset() {
    uint64_t now = Simulator::self()->circTime();
    for ( int i = 0; i < m_nTimers; ++i ) {
        Simulator::self()->cancelEvents( m_timers[i] );
        m_timers[i]->m_leds.clear();
        m_timers[i]->m_period = 0;
        m_timers[i]->m_epoch = now;
        m_timers[i]->m_dutyRes = 0;
        m_timers[i]->m_rawConf = 0;
        m_timers[i]->m_activeConf = 0;
    }

    for ( int i = 0; i < m_nChannels; ++i ) {
        Simulator::self()->cancelEvents( m_leds[i] );
        m_leds[i]->m_duty = 0;
        m_leds[i]->m_cycleHigh = 0;
        m_leds[i]->m_ditherAccumulator = 0;
        m_leds[i]->m_rawConf0 = 0;
        m_leds[i]->m_rawHpoint = 0;
        m_leds[i]->m_rawDuty = 0;
        m_leds[i]->m_rawConf1 = 0;
        m_leds[i]->m_activeConf0 = 0;
        m_leds[i]->m_activeHpoint = 0;
        m_leds[i]->m_activeConf1 = 0;
        m_leds[i]->m_fadeActive = false;
        m_leds[i]->m_overflowCount = 0;
        m_leds[i]->m_enabled = false;
        m_leds[i]->m_output.setOutputEnable( false );
        m_leds[i]->m_idleLevel = false;
        m_leds[i]->setOutput( false );
        m_leds[i]->m_timer = nullptr;
        m_leds[i]->setTimer( m_timers[0] );
    }

    m_rawApbClkSel = 0;
    m_interruptRaw = 0;
    m_interruptEnable = 0;
    m_irqLevel = true;
    updateInterrupt();
}

bool Esp32Led::isLowSpeedChannel( int ch ) const {
    return m_variant != LedcVariant::Esp32 || ch >= 8;
}

bool Esp32Led::isLowSpeedTimer( int timer ) const {
    return m_variant != LedcVariant::Esp32 || timer >= 4;
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
    uint64_t intBase = ( m_variant == LedcVariant::Esp32 ) ? 0x180 : 0x0C0;
    uint64_t confOffset = ( m_variant == LedcVariant::Esp32 ) ? 0x190 : 0x0D0;

    if ( offset == intBase + 0x08 ) { // INT_ENA
        m_interruptEnable = m_eventValue;
        updateInterrupt();
        return;
    }
    if ( offset == intBase + 0x0C ) { // INT_CLR
        m_interruptRaw &= ~m_eventValue;
        updateInterrupt();
        return;
    }

    if ( offset == confOffset ) {
        m_rawApbClkSel = m_eventValue;
        for ( int i = 0; i < m_nTimers; ++i )
            updateTimer( i );
        return;
    }

    int ch = channelFromOffset( offset );
    if ( ch >= 0 ) {
        uint64_t base = ( m_variant == LedcVariant::Esp32 && offset >= 0x0A0 ) ? 0x0A0 : 0x000;
        uint64_t sub = ( offset - base ) % 0x14;

        switch ( sub ) {
        case 0x000: { // CHx_CONF0
            m_leds[ch]->m_rawConf0 = m_eventValue;
            if ( !isLowSpeedChannel( ch ) || ( m_eventValue & ( 1 << 4 ) ) )
                commitChannel( ch );
        } break;
        case 0x004: // HPOINT
            m_leds[ch]->m_rawHpoint = m_eventValue;
            if ( !isLowSpeedChannel( ch ) )
                commitChannel( ch );
            break;
        case 0x008: { // CHx_DUTY
            m_leds[ch]->m_rawDuty = m_eventValue;
            if ( !isLowSpeedChannel( ch ) )
                commitChannel( ch );
        } break;
        case 0x00C: // CHx_CONF1
            m_leds[ch]->m_rawConf1 = m_eventValue;
            if ( !isLowSpeedChannel( ch ) )
                commitChannel( ch );
            break;
        default: // 0x010 DUTY_R (read only)
            break;
        }
        return;
    }

    int t = timerFromOffset( offset );
    if ( t >= 0 && ( offset % 8 ) == 0 ) { // TIMERx_CONF
        m_timers[t]->m_rawConf = m_eventValue;
        uint32_t updateBit = ( m_variant == LedcVariant::Esp32 ) ? 26 : 25;
        if ( !isLowSpeedTimer( t ) || ( m_eventValue & ( 1u << updateBit ) ) )
            commitTimer( t );
    }
}

void Esp32Led::commitChannel( int ch ) {
    LedPwm* pwm = m_leds[ch];
    if ( isLowSpeedChannel( ch ) )
        pwm->m_rawConf0 &= ~( 1u << 4 );

    pwm->m_activeConf0 = pwm->m_rawConf0;
    pwm->m_activeHpoint = pwm->m_rawHpoint;
    pwm->m_activeConf1 = pwm->m_rawConf1;
    pwm->m_duty = pwm->m_rawDuty;
    pwm->m_cycleHigh = pwm->m_duty >> 4;
    pwm->m_ditherAccumulator = pwm->m_duty & 0xF;

    int timer = pwm->m_activeConf0 & 0b11;
    if ( m_variant == LedcVariant::Esp32 && ch >= 8 )
        timer += 4;
    pwm->setTimer( m_timers[timer] );

    pwm->m_enabled = ( pwm->m_activeConf0 >> 2 ) & 1;
    pwm->m_idleLevel = ( pwm->m_activeConf0 >> 3 ) & 1;
    // SIG_OUT_EN selects the waveform or IDLE_LV; the LEDC matrix output
    // remains actively driven in both states.
    pwm->m_output.setOutputEnable( true );

    if ( m_variant != LedcVariant::Esp32 && ( pwm->m_activeConf0 & ( 1u << 16 ) ) )
        pwm->m_overflowCount = 0;

    uint32_t conf1 = pwm->m_activeConf1;
    if ( conf1 & ( 1u << 31 ) ) {
        pwm->m_fadeScale = conf1 & 0x3FF;
        pwm->m_fadeCycle = ( conf1 >> 10 ) & 0x3FF;
        pwm->m_fadeRemaining = ( conf1 >> 20 ) & 0x3FF;
        pwm->m_fadeIncrease = ( conf1 >> 30 ) & 1;
        pwm->m_fadeCycleCount = 0;
        pwm->m_fadeActive = pwm->m_fadeScale && pwm->m_fadeRemaining;
        if ( !pwm->m_fadeActive ) {
            pwm->m_activeConf1 &= ~( 1u << 31 );
            pwm->m_rawConf1 &= ~( 1u << 31 );
        }
    }

    if ( !pwm->m_enabled )
        pwm->setOutput( pwm->m_idleLevel );
    else
        pwm->ovf( m_timers[timer]->m_period );
}

void Esp32Led::commitTimer( int timer ) {
    LedTimer* ledTimer = m_timers[timer];
    ledTimer->m_activeConf = ledTimer->m_rawConf;
    if ( isLowSpeedTimer( timer ) ) {
        uint32_t updateBit = ( m_variant == LedcVariant::Esp32 ) ? 26 : 25;
        ledTimer->m_rawConf &= ~( 1u << updateBit );
        ledTimer->m_activeConf &= ~( 1u << updateBit );
    }
    updateTimer( timer );
}

void Esp32Led::updateTimer( int t ) {
    uint32_t val = m_timers[t]->m_activeConf;
    uint32_t div, dutyRes;
    bool pause, rst, tickSel;

    if ( m_variant == LedcVariant::Esp32 ) {
        dutyRes = val & 0x1F;          // [4:0]
        div = ( val >> 5 ) & 0x3FFFF;  // [22:5]
        pause = ( val >> 23 ) & 1;
        rst = ( val >> 24 ) & 1;
        tickSel = ( val >> 25 ) & 1;   // 1 = APB, 0 = REF_TICK
    } else { // S3 / C3
        dutyRes = val & 0xF;           // [3:0]
        div = ( val >> 4 ) & 0x3FFFF;  // [21:4]
        pause = ( val >> 22 ) & 1;
        rst = ( val >> 23 ) & 1;
        tickSel = m_variant == LedcVariant::Esp32s3 && ( ( val >> 24 ) & 1 );
    }

    m_timers[t]->m_dutyRes = dutyRes;

    if ( pause || rst || div == 0 ) {
        m_timers[t]->setPeriod( 0 );
    } else {
        // clk selected by the driver:
        // ESP32:  tick_sel=1 -> APB (80 MHz), tick_sel=0 -> REF_TICK (1 MHz)
        // S3/C3:  tick_sel=1 -> REF_TICK (1 MHz)
        //         tick_sel=0 -> conf.apb_clk_sel: 0 -> APB (80 MHz),
        //                                        1 -> RC_FAST (8 MHz),
        //                                        2 -> XTAL (40 MHz)
        uint64_t clk;
        if ( m_variant == LedcVariant::Esp32 ) {
            clk = tickSel ? 80000000 : 1000000;
        } else if ( tickSel ) {
            clk = 1000000;
        } else {
            uint32_t apbSel = m_rawApbClkSel & 0b11;
            clk = ( apbSel == 0 ) ? 80000000 : ( apbSel == 1 ) ? 8000000 : 40000000;
        }
        // f_pwm = clk*256 / (clock_divider * 2^duty_res)
        long double ps = (long double)( 1ULL << dutyRes ) * div * 1e12L / ( (long double)clk * 256.0L );
        m_timers[t]->setPeriod( (uint64_t)ps );
    }
}

void Esp32Led::readRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t value = 0;
    uint64_t intBase = ( m_variant == LedcVariant::Esp32 ) ? 0x180 : 0x0C0;
    uint64_t confOffset = ( m_variant == LedcVariant::Esp32 ) ? 0x190 : 0x0D0;

    if ( offset == intBase ) {
        value = m_interruptRaw;
    } else if ( offset == intBase + 0x04 ) {
        value = m_interruptRaw & m_interruptEnable;
    } else if ( offset == intBase + 0x08 ) {
        value = m_interruptEnable;
    } else if ( offset == confOffset ) {
        value = m_rawApbClkSel;
    } else {
        int ch = channelFromOffset( offset );
        if ( ch >= 0 ) {
            uint64_t base = ( m_variant == LedcVariant::Esp32 && offset >= 0x0A0 ) ? 0x0A0 : 0x000;
            uint64_t sub = ( offset - base ) % 0x14;
            switch ( sub ) {
            case 0x000: value = m_leds[ch]->m_rawConf0; break; // CHx_CONF0
            case 0x004: value = m_leds[ch]->m_rawHpoint; break; // CHx_HPOINT
            case 0x008: value = m_leds[ch]->m_rawDuty; break;   // CHx_DUTY
            case 0x00C: value = m_leds[ch]->m_rawConf1; break;  // CHx_CONF1
            case 0x010: value = m_leds[ch]->m_duty; break;      // CHx_DUTY_R
            }
        } else {
            int t = timerFromOffset( offset );
            if ( t >= 0 ) {
                if ( ( offset % 8 ) == 0 ) // TIMERx_CONF
                    value = m_timers[t]->m_rawConf;
                else                       // TIMERx_VALUE
                    value = m_timers[t]->readCounter();
            }
        }
    }

    m_arena->regData = value;
    m_arena->qemuAction = SIM_READ;
}

void Esp32Led::timerOverflow( int timer ) {
    m_interruptRaw |= 1u << timer;
    updateInterrupt();
}

void Esp32Led::fadeEnded( int channel ) {
    uint32_t bit = ( m_variant == LedcVariant::Esp32 ) ? 8 + channel : 4 + channel;
    m_interruptRaw |= 1u << bit;
    updateInterrupt();
}

void Esp32Led::channelOverflowEnded( int channel ) {
    if ( m_variant == LedcVariant::Esp32 )
        return;
    uint32_t bit = 12 + channel;
    if ( m_variant == LedcVariant::Esp32c3 )
        bit = 10 + channel;
    m_interruptRaw |= 1u << bit;
    updateInterrupt();
}

void Esp32Led::updateInterrupt() {
    bool level = ( m_interruptRaw & m_interruptEnable ) != 0;
    if ( level == m_irqLevel )
        return;
    m_irqLevel = level;
    uint8_t source = m_variant == LedcVariant::Esp32 ? 43 : m_variant == LedcVariant::Esp32s3 ? 35 : 23;
    setInterrupt( source, level );
}

Esp32OutputSignal* Esp32Led::getOutputSignal( int n ) {
    if ( n >= m_nChannels )
        return nullptr;
    return &m_leds[n]->m_output;
}

// -------------------------------------------------

LedPwm::LedPwm( QString id, Esp32Led* owner, int channel ) : eElement( id ) {
    m_owner = owner;
    m_channel = channel;
    m_timer = nullptr;
}
LedPwm::~LedPwm() { }

void LedPwm::initialize() {
    m_duty = 0;
    m_cycleHigh = 0;
    m_ditherAccumulator = 0;
    m_rawDuty = 0;
    m_enabled = false;
    m_idleLevel = false;
    m_timer = nullptr;
    m_edgeCount = 0;
    m_edgeIndex = 0;
    m_fadeActive = false;
    m_overflowCount = 0;
}

void LedPwm::scheduleEvents() {
    if ( m_edgeIndex < m_edgeCount )
        Simulator::self()->addEvent( m_edgeTimes[m_edgeIndex], this );
}

void LedPwm::ovf( uint64_t p ) {
    Simulator::self()->cancelEvents( this );
    m_edgeCount = 0;
    m_edgeIndex = 0;

    if ( !m_enabled || p == 0 ) {
        setOutput( m_idleLevel );
        return;
    }

    uint32_t dutyRes = m_timer ? m_timer->m_dutyRes : 0;
    if ( dutyRes >= 32 )
        dutyRes = 0;
    uint64_t ticks = 1ULL << dutyRes; // counter period in clock ticks

    uint64_t high = m_cycleHigh;
    if ( high == 0 ) {
        setOutput( false );
        return;
    }

    if ( high >= ticks ) {
        setOutput( true );
        return;
    }

    uint64_t hpoint = m_activeHpoint % ticks;
    uint64_t lpoint = ( hpoint + high ) % ticks;
    bool wraps = hpoint > lpoint;
    setOutput( wraps );

    auto addEdge = [&]( uint64_t tick, bool state ) {
        if ( !tick ) {
            setOutput( state );
            return;
        }
        uint64_t time = (uint64_t)( (long double)p * tick / ticks );
        if ( !time )
            time = 1;
        m_edgeTimes[m_edgeCount] = time;
        m_edgeStates[m_edgeCount] = state;
        ++m_edgeCount;
    };

    if ( wraps ) {
        addEdge( lpoint, false );
        addEdge( hpoint, true );
    } else {
        addEdge( hpoint, true );
        addEdge( lpoint, false );
    }

    if ( m_edgeCount == 2 && m_edgeTimes[0] > m_edgeTimes[1] ) {
        qSwap( m_edgeTimes[0], m_edgeTimes[1] );
        qSwap( m_edgeStates[0], m_edgeStates[1] );
    }
    scheduleEvents();
}

void LedPwm::runEvent() {
    if ( m_edgeIndex >= m_edgeCount )
        return;
    uint64_t previous = m_edgeTimes[m_edgeIndex];
    setOutput( m_edgeStates[m_edgeIndex] );
    ++m_edgeIndex;
    if ( m_edgeIndex < m_edgeCount ) {
        m_edgeTimes[m_edgeIndex] -= previous;
        scheduleEvents();
    }
}

void LedPwm::timerOverflow( uint64_t p ) {
    if ( m_fadeActive ) {
        ++m_fadeCycleCount;
        if ( m_fadeCycleCount >= qMax( 1u, m_fadeCycle ) ) {
            m_fadeCycleCount = 0;
            uint32_t step = m_fadeScale << 4;
            uint32_t resolution = m_timer ? qMin( m_timer->m_dutyRes, 27u ) : 0;
            uint32_t maximum = ( 1u << resolution ) << 4;
            if ( m_fadeIncrease )
                m_duty = qMin( maximum, m_duty + step );
            else
                m_duty = step > m_duty ? 0 : m_duty - step;

            if ( --m_fadeRemaining == 0 ) {
                m_fadeActive = false;
                m_activeConf1 &= ~( 1u << 31 );
                m_rawConf1 &= ~( 1u << 31 );
                m_owner->fadeEnded( m_channel );
            }
        }
    }

    if ( m_owner->m_variant != LedcVariant::Esp32 && ( m_activeConf0 & ( 1u << 15 ) ) ) {
        uint32_t target = ( m_activeConf0 >> 5 ) & 0x3FF;
        if ( target && ++m_overflowCount >= target ) {
            m_overflowCount = 0;
            m_owner->channelOverflowEnded( m_channel );
        }
    }

    m_cycleHigh = m_duty >> 4;
    m_ditherAccumulator += m_duty & 0xF;
    if ( m_ditherAccumulator >= 16 ) {
        m_ditherAccumulator -= 16;
        ++m_cycleHigh;
    }

    ovf( p );
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
    m_output.setState( state );
}
// -------------------------------------------------

LedTimer::LedTimer( QString id, Esp32Led* owner, int timer ) : eElement( id ) {
    m_owner = owner;
    m_timer = timer;
}
LedTimer::~LedTimer() { }

void LedTimer::initialize() {
    m_period = 0;
    m_epoch = Simulator::self()->circTime();
    m_dutyRes = 0;
    m_rawConf = 0;
    m_activeConf = 0;
    m_leds.clear();
}

void LedTimer::runEvent() {
    m_epoch = Simulator::self()->circTime();
    m_owner->timerOverflow( m_timer );
    for ( LedPwm* pwm : m_leds )
        pwm->timerOverflow( m_period );

    if ( m_period )
        Simulator::self()->addEvent( m_period, this );
}

void LedTimer::setPeriod( uint64_t p ) {
    if ( m_period == p )
        return;

    Simulator::self()->cancelEvents( this );
    m_period = p;
    m_epoch = Simulator::self()->circTime();

    if ( p )
        Simulator::self()->addEvent( p, this );

    for ( LedPwm* pwm : m_leds )
        pwm->ovf( p );
}

uint32_t LedTimer::readCounter() {
    if ( !m_period )
        return 0;

    uint32_t resolution = qMin( m_dutyRes, 31u );
    uint64_t ticks = 1ULL << resolution;
    uint64_t elapsed = Simulator::self()->circTime() - m_epoch;
    uint64_t phase = elapsed % m_period;
    return (uint32_t)( (long double)phase * ticks / m_period );
}

void LedTimer::addLedPwm( LedPwm* l ) {
    if ( !m_leds.contains( l ) )
        m_leds.append( l );
}

void LedTimer::remLedPwm( LedPwm* l ) {
    m_leds.removeOne( l );
}
