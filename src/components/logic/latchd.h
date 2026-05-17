/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class LibraryItem;

class LatchD : public LogicComponent {
public:
    LatchD( QString type, QString id );
    ~LatchD();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    int channels() { return m_channels; }
    void setChannels( int channels );

    bool pinReset() { return m_useReset; }
    void setPinReset( bool r );

    void setTristate( bool t ) override;
    void setTrigger( trigger_t trigger ) override;

private:
    void updateSize();

    int m_channels;

    bool m_useReset;

    IoPin* m_resetPin;
};
