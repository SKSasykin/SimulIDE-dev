/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "bcdbase.h"

class LibraryItem;

class SevenSegmentBCD : public BcdBase {
public:
    SevenSegmentBCD( QString type, QString id );
    ~SevenSegmentBCD();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void updateStep() override;
    void voltChanged() override;

    bool isShowEnablePin() { return m_showEnablePin; }
    void setShowEnablePin( bool show );

    bool isShowDotPin() { return m_showDotPin; }
    void setShowDotPin( bool show );

    bool setLinkedTo( Linker* li ) override;
    void setLinkedValue( double v, int i = 0 ) override;

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

private:
    bool m_showEnablePin;
    bool m_showDotPin;

    IoPin* m_dotPin;
    IoPin* m_enablePin;
};
