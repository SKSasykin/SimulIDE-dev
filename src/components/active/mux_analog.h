/***************************************************************************
 *   Copyright (C) 2019 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "e-element.h"

class eResistor;
class LibraryItem;

class MuxAnalog : public Component, public eElement {
public:
    MuxAnalog( QString type, QString id );
    ~MuxAnalog();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void updateStep() override;
    void voltChanged() override;
    void runEvent() override;
    void remove() override;

    int addrBits() { return m_addrBits; }
    void setAddrBits( int bits );

    double impedance() { return 1 / m_admit; }
    void setImpedance( double i );

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

private:
    void createAddrBits( int c );
    void deleteAddrBits( int d );
    void createResistors( int c );
    void deleteResistors( int d );

    int m_addrBits;
    int m_channels;
    int m_address;

    double m_admit;

    bool m_enabled;

    Pin* m_zPin;
    Pin* m_enPin;

    std::vector<eResistor*> m_resistor;
    std::vector<Pin*> m_addrPin;
    std::vector<Pin*> m_chanPin;
};
