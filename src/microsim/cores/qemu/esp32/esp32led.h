/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "e-element.h"
#include "qemumodule.h"

class Esp32Pin;
class LedTimer;
class LedPwm;

enum class LedcVariant { Esp32, Esp32s3, Esp32c3 };

class Esp32Led : public QemuModule {
public:
    Esp32Led( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
              LedcVariant variant, int nChannels, int nTimers );
    ~Esp32Led();

    void reset() override;

    void setDummy( IoPin* pin );

    IoPin** getPinPtr( int n );

private:
    int channelFromOffset( uint64_t offset );
    int timerFromOffset( uint64_t offset );

    void writeRegister() override;
    void readRegister() override;

    LedcVariant m_variant;
    int m_nChannels;
    int m_nTimers;

    LedTimer* m_timers[8];
    LedPwm* m_leds[16];
};
// -------------------------------------------------

class LedPwm : public eElement {
    friend class Esp32Led;

public:
    LedPwm( QString id );
    ~LedPwm();

    void initialize() override;
    void runEvent() override;

    void ovf( uint64_t p );
    void scheduleEvents();

    void setTimer( LedTimer* t );
    void setOutput( bool state );

private:
    uint64_t m_matchTime;
    uint32_t m_duty;
    bool m_enabled;
    bool m_idleLevel;

    IoPin* m_pin;

    LedTimer* m_timer;
};
// -------------------------------------------------

class LedTimer : public eElement {
    friend class Esp32Led;
    friend class LedPwm;

public:
    LedTimer( QString id );
    ~LedTimer();

    void initialize() override;
    void runEvent() override;

    void setPeriod( uint64_t p );
    uint32_t readCounter();

    void addLedPwm( LedPwm* l );
    void remLedPwm( LedPwm* l );

private:
    uint64_t m_period;
    uint32_t m_dutyRes;

    QList<LedPwm*> m_leds;
};