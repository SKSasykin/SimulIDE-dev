/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class IoPin;

class Gate : public LogicComponent {
public:
    Gate( QString type, QString id, int inputs );
    ~Gate();

    QList<ComProperty*> outputProps();

    bool propNotFound( QString prop, QString val ) override;

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    bool initHigh() { return m_initState; }
    void setInitHigh( bool s ) { m_initState = s; }

    void setNumInputs( int pins );

    QPainterPath shape() const override;

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

protected:
    virtual bool calcOutput( int inputs );
    virtual void updatePath() { ; }

    bool m_initState;

    int m_minInputs;

    QPainterPath m_path;
};
