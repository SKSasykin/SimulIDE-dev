/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "e-element.h"
#include "qemumodule.h"
#include "esp32pin.h"

class Esp32Pin;
class LedTimer;
class LedPwm;

enum class LedcVariant { Esp32, Esp32s3, Esp32c3 };

class Esp32Led : public QemuModule {
    friend class LedTimer;
    friend class LedPwm;

public:
    Esp32Led( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
              LedcVariant variant, int nChannels, int nTimers );
    ~Esp32Led();

    void reset() override;

    Esp32OutputSignal* getOutputSignal( int n );

private:
    int channelFromOffset( uint64_t offset );
    int timerFromOffset( uint64_t offset );
    bool isLowSpeedChannel( int ch ) const;
    bool isLowSpeedTimer( int timer ) const;
    void commitChannel( int ch );
    void commitTimer( int timer );
    void updateTimer( int t );
    void timerOverflow( int timer );
    void fadeEnded( int channel );
    void channelOverflowEnded( int channel );
    void updateInterrupt();

    void writeRegister() override;
    void readRegister() override;

    LedcVariant m_variant;
    int m_nChannels;
    int m_nTimers;

    uint32_t m_rawApbClkSel;
    uint32_t m_interruptRaw;
    uint32_t m_interruptEnable;
    bool m_irqLevel;

    LedTimer* m_timers[8];
    LedPwm* m_leds[16];
};
// -------------------------------------------------

class LedPwm : public eElement {
    friend class Esp32Led;

public:
    LedPwm( QString id, Esp32Led* owner, int channel );
    ~LedPwm();

    void initialize() override;
    void runEvent() override;

    void ovf( uint64_t p );
    void timerOverflow( uint64_t p );
    void scheduleEvents();

    void setTimer( LedTimer* t );
    void setOutput( bool state );

private:
    Esp32Led* m_owner;
    int m_channel;
    uint64_t m_edgeTimes[2];
    bool m_edgeStates[2];
    int m_edgeCount;
    int m_edgeIndex;

    uint32_t m_duty;
    uint32_t m_cycleHigh;
    uint8_t m_ditherAccumulator;
    uint32_t m_rawConf0;
    uint32_t m_rawHpoint;
    uint32_t m_rawDuty;
    uint32_t m_rawConf1;
    uint32_t m_activeConf0;
    uint32_t m_activeHpoint;
    uint32_t m_activeConf1;

    uint32_t m_fadeRemaining;
    uint32_t m_fadeCycle;
    uint32_t m_fadeCycleCount;
    uint32_t m_fadeScale;
    bool m_fadeIncrease;
    bool m_fadeActive;
    uint32_t m_overflowCount;

    bool m_enabled;
    bool m_idleLevel;

    Esp32OutputSignal m_output;

    LedTimer* m_timer;
};
// -------------------------------------------------

class LedTimer : public eElement {
    friend class Esp32Led;
    friend class LedPwm;

public:
    LedTimer( QString id, Esp32Led* owner, int timer );
    ~LedTimer();

    void initialize() override;
    void runEvent() override;

    void setPeriod( uint64_t p );
    uint32_t readCounter();

    void addLedPwm( LedPwm* l );
    void remLedPwm( LedPwm* l );

private:
    Esp32Led* m_owner;
    int m_timer;
    uint64_t m_period;
    uint64_t m_epoch;
    uint32_t m_dutyRes;
    uint32_t m_rawConf;
    uint32_t m_activeConf;

    QList<LedPwm*> m_leds;
};
