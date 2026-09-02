/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QFont>

#include "component.h"
#include "iopin.h"
#include "spimodule.h"

class LibraryItem;

class Max31855 : public Component, public SpiModule {
public:
    Max31855( QString type, QString id );
    ~Max31855();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void voltChanged() override;

    void endTransaction() override;
    void ssChanged( bool enable ) override;

    double temp() { return m_temp; }
    void setTemp( double t );

    bool grounded() { return m_grounded; }
    void setGrounded( bool g );

    double tempInc() { return m_tempInc; }
    void setTempInc( double inc ) { m_tempInc = inc; }

    double internalTemp() { return m_internalTemp; }
    void setInternalTemp( double t );

    bool oc() { return m_oc; }
    void setOc( bool b );

    bool scg() { return m_scg; }
    void setScg( bool b );

    bool scv() { return m_scv; }
    void setScv( bool b );

    void updateStep() override;

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

public slots:
    void upbuttonclicked();
    void downbuttonclicked();

protected:
    void buildData();

    double m_temp;
    double m_tempInc;
    double m_internalTemp;

    bool m_grounded;
    bool m_oc;
    bool m_scg;
    bool m_scv;

    uint8_t m_sendData[4];
    uint8_t m_byteIndex;

    QFont m_font;

    IoPin m_pinCS;
    IoPin m_pinDI;
    IoPin m_pinCK;
    IoPin m_pinDO;

    Pin m_gnd;
};
