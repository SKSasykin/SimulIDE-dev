/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class FlipFlopBase : public LogicComponent {
public:
    FlipFlopBase( QString type, QString id );
    ~FlipFlopBase();

    //bool srInv() { return m_srInv; }
    //void setSrInv( bool inv );

    bool propNotFound( QString prop, QString val ) override;

    bool pinsRS() { return m_useRS; }
    void usePinsRS( bool rs );

    void stamp() override;
    void updateStep() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    bool sPinState();
    bool rPinState();

protected:
    virtual void calcOutput() { ; }

    bool m_useRS;
    //bool m_srInv;
    bool m_Q0;

    int m_dataPins;

    IoPin* m_setPin;
    IoPin* m_rstPin;
};
