/***************************************************************************
 *   Copyright (C) 2026 by SimulIDE developers                             *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "clock-base.h"

class LibraryItem;

class AcVoltage : public ClockBase {
public:
    AcVoltage( QString type, QString id );
    ~AcVoltage();

    static Component* constructFixed( QString type, QString id );
    static Component* constructRail( QString type, QString id );
    static LibraryItem* fixedLibraryItem();
    static LibraryItem* railLibraryItem();

    void setup() override;
    void initialize() override;
    void stamp() override;
    void runEvent() override;
    void updateStep() override;

    double voltageRms() { return m_voltageRms; }
    void setVoltageRms( double voltage );
    void setFreq( double frequency ) override;

public slots:
    void onbuttonclicked() override;

protected:
    QPainterPath shape() const override;
    void paint( QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

private:
    void setOutputVoltage();

    bool m_isRail;
    double m_voltageRms;
    double m_eventStep;
    double m_eventRemainder;
    uint64_t m_eventStepInt;
};
