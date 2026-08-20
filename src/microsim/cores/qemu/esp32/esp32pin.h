/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "iopin.h"
#include "qemumodule.h"

class Esp32Pin;

class Esp32OutputSignal {
public:
    void connectPad( Esp32Pin* pin );
    void disconnectPad( Esp32Pin* pin );
    void setState( bool state );
    void setOutputEnable( bool enabled );

    bool state() const { return m_state; }
    bool outputEnable() const { return m_outputEnable; }

private:
    QList<Esp32Pin*> m_pads;
    bool m_state = false;
    bool m_outputEnable = false;
};

struct funcPin {
    funcPin( QemuModule* m = nullptr, IoPin** p = nullptr, QString l = QString(), Esp32OutputSignal* signal = nullptr )
        : module( m ), pinPointer( p ), label( l ), outputSignal( signal ) { }

    QemuModule* module;
    IoPin** pinPointer;
    QString label;
    Esp32OutputSignal* outputSignal;
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

    void setMatrixFunc( uint16_t val, funcPin func );
    void setMatrixOutput( uint16_t val, funcPin func );
    void outputSignalChanged( Esp32OutputSignal* signal );
    void resetMatrixOutput();
    void setMatrixMuxIndex( uint8_t index ) { m_matrixMuxIndex = index; }

    void setIoMuxFuncs( QList<funcPin> functions );

    void writePinReg( uint32_t value );

protected:
    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    void selectIoMuxFunc( uint8_t func );

    void setPinState( bool high );
    void refreshMatrixOutput();
    QString m_pinLabel;

    //bool m_analog;
    //bool m_alternate;

    double m_pullAdmit;

    uint64_t m_pinMask;
    uint8_t m_pullUp;
    uint8_t m_pullDown;
    uint8_t m_inputEn;

    uint8_t m_iomuxIndex;
    uint8_t m_matrixMuxIndex;

    uint16_t m_matrixOutConfig;
    bool m_gpioState;
    bool m_gpioOutputEnable;

    IoPin* m_dummyPin;
    funcPin m_iomuxFuncs[6];
};
