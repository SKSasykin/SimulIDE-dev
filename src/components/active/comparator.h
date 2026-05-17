/***************************************************************************
 *   Copyright (C) 2024 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "e-element.h"
#include "iocomponent.h"

class LibraryItem;

class Comparator : public IoComponent, public eElement {
public:
    Comparator( QString type, QString id );
    ~Comparator();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    QPainterPath shape() const override;

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};
