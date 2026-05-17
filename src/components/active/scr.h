/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "e-element.h"

class LibraryItem;
class eDiode;
class eResistor;

class SCR : public Component, public eElement {
public:
    SCR( QString type, QString id );
    ~SCR();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void initialize() override;
    void stamp() override;
    void updateStep() override;
    void voltChanged() override;

    double gateRes() { return m_gateRes; }
    void setGateRes( double r ) {
        m_gateRes = r;
        m_changed = true;
    }

    double trigCurr() { return m_trigCurr; }
    void setTrigCurr( double c ) {
        m_trigCurr = c;
        m_changed = true;
    }

    double holdCurr() { return m_holdCurr; }
    void setHoldCurr( double v ) {
        m_holdCurr = v;
        m_changed = true;
    }

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

private:
    double m_gateRes;
    double m_trigCurr;
    double m_holdCurr;

    bool m_state;

    eNode* m_midEnode;
    eResistor* m_resistor;
    eResistor* m_resistGa;
    eDiode* m_diode;
};
