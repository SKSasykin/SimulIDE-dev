/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class LibraryItem;

class Mux : public LogicComponent {
public:
    QRectF boundingRect() const override { return m_area; }

    Mux( QString type, QString id );
    ~Mux();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    int addrBits() { return m_addrBits; }
    void setAddrBits( int bits );

    QPainterPath shape() const override;
    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

private:
    int m_addrBits;
};
