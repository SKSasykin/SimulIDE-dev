/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class LibraryItem;

class Demux : public LogicComponent {
public:
    Demux( QString type, QString id );
    ~Demux();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    int addrBits() { return m_addrBits; }
    void setAddrBits( int bits );

    QPainterPath shape() const;
    void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget );

private:
    int m_addrBits;
};
