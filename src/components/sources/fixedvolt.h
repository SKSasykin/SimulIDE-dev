/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QGraphicsProxyWidget>
#include <QToolButton>

#include "component.h"
#include "e-element.h"

class IoPin;
class LibraryItem;
class CustomButton;

class FixedVolt : public Component, public eElement {
public:
    FixedVolt( QString type, QString id );
    ~FixedVolt();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void setSmall( bool s );
    bool isSmall() { return m_small; }

    void stamp() override;
    void updateStep() override;

    bool out();
    virtual void setOut( bool out );

    double volt() { return m_voltage; }
    void setVolt( double v );

public slots:
    virtual void onbuttonclicked();

protected:
    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
    void updateOutput();

    double m_voltage;

    bool m_small;

    IoPin* m_outpin;

    CustomButton* m_button;
    QGraphicsProxyWidget* m_proxy;
};
