/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "e-diode.h"
#include "linkercomponent.h"

class LibraryItem;

class Diode : public LinkerComponent, public eDiode {
public:
    Diode( QString type, QString id, bool zener = false );
    ~Diode();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void voltChanged() override;

    bool propNotFound( QString prop, QString val ) override;

    void initialize() override;
    void stamp() override;
    void updateStep() override;

    double resistance() override { return m_resistor->resistance(); }
    void setResSafe( double resist ) override { m_resistor->setResSafe( resist ); }

    void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

private:
    bool m_isZener;

    eNode* m_midEnode;
    eResistor* m_resistor;
};
