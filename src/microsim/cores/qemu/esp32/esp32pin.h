/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "iopin.h"
#include "qemumodule.h"
#include "e-element.h"

class Esp32Pin;
class eElement;

class Esp32OutputSignal : public eElement {
public:
    enum DriveMode { PushPull, OpenDrain };

    Esp32OutputSignal();

    void connectPad( Esp32Pin* pin );
    void disconnectPad( Esp32Pin* pin );
    void setState( bool state );
    void scheduleState( bool state, uint64_t delay );
    void resetState( bool state );
    void setOutputEnable( bool enabled );
    void setDriveMode( DriveMode mode );
    void runEvent() override;

    bool state() const { return m_state; }
    bool outputEnable() const { return m_outputEnable; }
    DriveMode driveMode() const { return m_driveMode; }
    bool routed() const { return !m_pads.isEmpty(); }

private:
    QList<Esp32Pin*> m_pads;
    bool m_state = false;
    bool m_scheduledState = false;
    bool m_statePending = false;
    bool m_outputEnable = false;
    DriveMode m_driveMode = PushPull;
};

class Esp32InputSignal {
public:
    enum Constant { NoConstant, ConstantLow, ConstantHigh };

    void setIoMuxRoute( Esp32Pin* pin );
    void clearIoMuxRoute( Esp32Pin* pin = nullptr );
    void setMatrixRoute( Esp32Pin* pin, bool inverted );
    void setMatrixConstant( Constant constant, bool inverted );
    void selectMatrix( bool selected );
    void clearMatrixRoute();
    void clearRoutes();
    bool state() const;
    bool routed() const;
    void watch( eElement* listener, bool enabled );

private:
    Esp32Pin* activePin() const;
    void beginRouteChange();
    void endRouteChange();

    Esp32Pin* m_ioMuxPin = nullptr;
    Esp32Pin* m_matrixPin = nullptr;
    eElement* m_listener = nullptr;
    Constant m_matrixConstant = NoConstant;
    bool m_matrixInverted = false;
    bool m_matrixSelected = false;
    bool m_watching = false;
};

struct inputFunc {
    inputFunc( QemuModule* m = nullptr, IoPin** p = nullptr, QString l = QString(), Esp32InputSignal* signal = nullptr )
        : module( m ), pinPointer( p ), label( l ), inputSignal( signal ) { }

    QemuModule* module;
    IoPin** pinPointer;
    QString label;
    Esp32InputSignal* inputSignal;
};

struct funcPin {
    funcPin( QemuModule* m = nullptr, IoPin** p = nullptr, QString l = QString(), Esp32OutputSignal* output = nullptr,
             Esp32InputSignal* input = nullptr )
        : module( m ), pinPointer( p ), label( l ), outputSignal( output ), inputSignal( input ) { }
    funcPin( const inputFunc& input )
        : module( input.module ), pinPointer( input.pinPointer ), label( input.label ), outputSignal( nullptr ),
          inputSignal( input.inputSignal ) { }

    QemuModule* module;
    IoPin** pinPointer;
    QString label;
    Esp32OutputSignal* outputSignal;
    Esp32InputSignal* inputSignal;
};

class Esp32Pin : public IoPin //, public QemuModule
{
    friend class Esp32;

public:
    Esp32Pin( int i, QString id, QemuDevice* mcu, IoPin* dummyPin );
    ~Esp32Pin();

    void initialize() override;
    void stamp() override;
    void updateStep() override;
    void voltChanged() override;

    void setPinMode( pinMode_t mode );

    void setOutState( bool high ) override;
    void scheduleState( bool high, uint64_t time ) override;

    void setPortState( bool high );
    void setGpioState( bool high );
    void setGpioOutputEnable( bool enabled );

    //void setPull( bool p );
    //bool setAlternate( bool a );
    //void setAnalog( bool a );

    void writeIoMuxReg( uint16_t value );
    void setInternalPullup( bool enabled );
    void setInternalPulldown( bool enabled );
    void setRtcPullup( bool enabled );
    void setRtcPulldown( bool enabled );

    void setMatrixFunc( uint16_t val, funcPin func );
    void setMatrixOutput( uint16_t val, funcPin func );
    void outputSignalChanged( Esp32OutputSignal* signal );
    void resetMatrixOutput();
    void resetRoutes();
    void setMatrixMuxIndex( uint8_t index ) { m_matrixMuxIndex = index; }

    void setIoMuxFuncs( QList<funcPin> functions );

    void writePinReg( uint32_t value );

protected:
    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    void disconnectIoMuxFunc();
    void selectIoMuxFunc( uint8_t func );

    void setPinState( bool high );
    void refreshMatrixOutput();
    void updateInternalPullup();
    void updateInternalPulldown();
    QString m_pinLabel;

    //bool m_analog;
    //bool m_alternate;

    double m_pullResistance;

    uint64_t m_pinMask;
    uint8_t m_pullUp;
    uint8_t m_pullDown;
    uint8_t m_inputEn;
    bool m_iomuxPullUp;
    bool m_iomuxPullDown;
    bool m_rtcPullUp;
    bool m_rtcPullDown;
    bool m_rtcPullControl;

    uint8_t m_iomuxIndex;
    uint8_t m_matrixMuxIndex;

    uint16_t m_matrixOutConfig;
    bool m_gpioState;
    bool m_gpioOutputEnable;

    IoPin* m_dummyPin;
    funcPin m_iomuxFuncs[6];
};
