/***************************************************************************
 *   Copyright (C) 2026 by SimulIDE contributors                           *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QDialog>
#include <QMutex>
#include <QPainterPath>
#include <QVector>
#include <QWidget>

#include "component.h"
#include "e-element.h"
#include "e-resistor.h"
#include "pin.h"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QScrollBar;
class LibraryItem;

class Heater;

class HeaterPlot : public QWidget {
public:
    explicit HeaterPlot( QWidget* parent = nullptr );

    void setData( const QVector<double>& time, const QVector<double>& bodyTemp,
                  const QVector<double>& sensorTemp );
    void setWindowSeconds( double seconds );
    void setViewEnd( double seconds );

protected:
    void paintEvent( QPaintEvent* event ) override;

private:
    QVector<double> m_time;
    QVector<double> m_bodyTemp;
    QVector<double> m_sensorTemp;
    double m_windowSeconds;
    double m_viewEnd;
};

class HeaterDialog : public QDialog {
public:
    HeaterDialog( QWidget* parent, Heater* heater );

    void setWindowSeconds( double seconds );
    void updateData( const QVector<double>& time, const QVector<double>& bodyTemp,
                     const QVector<double>& sensorTemp, double power1, double power2,
                     double capacity, double loss, double sensorTau );

private:
    void updateViewEnd();

    Heater* m_heater;
    HeaterPlot* m_plot;
    QLabel* m_status;
    QDoubleSpinBox* m_windowBox;
    QCheckBox* m_followBox;
    QScrollBar* m_scrollBar;
    double m_firstTime;
    double m_lastTime;
};

class Heater : public Component, public eElement {
public:
    Heater( QString type, QString id );
    ~Heater();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void initialize() override;
    void stamp() override;
    void runEvent() override;
    void voltChanged() override;
    void updateStep() override;

    double heater1Power() { return m_ratedPower1; }
    void setHeater1Power( double power );
    double heater2Power() { return m_ratedPower2; }
    void setHeater2Power( double power );

    double heater1Voltage() { return m_ratedVoltage1; }
    void setHeater1Voltage( double voltage );
    double heater2Voltage() { return m_ratedVoltage2; }
    void setHeater2Voltage( double voltage );

    double heater1Resistance() { return m_resistance1; }
    void setHeater1Resistance( double resistance );
    double heater2Resistance() { return m_resistance2; }
    void setHeater2Resistance( double resistance );
    double wireTemperatureCoefficient() { return m_wireTemperatureCoefficient; }
    void setWireTemperatureCoefficient( double coefficient );

    QString supplyType() { return m_supplyType; }
    void setSupplyType( QString type );

    QString material() { return m_material; }
    void setMaterial( QString material );
    QString environment() { return m_environment; }
    void setEnvironment( QString environment );

    double bodyLength() { return m_length; }
    void setBodyLength( double length );
    double bodyWidth() { return m_width; }
    void setBodyWidth( double width );
    double bodyHeight() { return m_height; }
    void setBodyHeight( double height );

    double ambientTemperature() { return m_ambientTemp; }
    void setAmbientTemperature( double temperature );
    double initialTemperature() { return m_initialTemp; }
    void setInitialTemperature( double temperature );
    double airCoefficient() { return m_airCoefficient; }
    void setAirCoefficient( double coefficient );
    double emissivity() { return m_emissivity; }
    void setEmissivity( double emissivity );
    double sensorDistance() { return m_sensorDistance; }
    void setSensorDistance( double distance );
    double thermocoupleSensitivity() { return m_tcSensitivity; }
    void setThermocoupleSensitivity( double sensitivity );
    double thermocoupleResistance() { return m_tcResistance; }
    void setThermocoupleResistance( double resistance );
    double thermalStep() { return m_thermalStep; }
    void setThermalStep( double step );

    double plotWindow() { return m_plotWindow; }
    void setPlotWindow( double seconds );
    int historySize() { return m_historySize; }
    void setHistorySize( int size );

    double bodyTemperature() { return m_bodyTemp; }
    double sensorTemperature() { return m_sensorTemp; }
    double thermalCapacity() const;
    double heatLossCoefficient() const;
    double sensorTimeConstant() const;

    void openPlot();

    QPainterPath shape() const override;
    void contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) override;

protected:
    void paint( QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

private:
    void advanceThermal( uint64_t time, bool stampOutput = true );
    void updatePower();
    void stampThermocouple();
    void appendHistory( uint64_t time );
    void updateGeometryValues();
    double resistanceAtTemperature( double referenceResistance ) const;

    eResistor m_heater1;
    eResistor m_heater2;
    eResistor m_tcOutput;

    Pin m_h1Plus;
    Pin m_h1Minus;
    Pin m_tcPlus;
    Pin m_tcMinus;
    Pin m_h2Plus;
    Pin m_h2Minus;

    HeaterDialog* m_dialog;

    QString m_supplyType;
    QString m_material;
    QString m_environment;

    double m_ratedPower1;
    double m_ratedPower2;
    double m_ratedVoltage1;
    double m_ratedVoltage2;
    double m_resistance1;
    double m_resistance2;
    double m_wireTemperatureCoefficient;

    double m_length;
    double m_width;
    double m_height;
    double m_density;
    double m_specificHeat;
    double m_thermalConductivity;
    double m_airCoefficient;
    double m_emissivity;
    double m_sensorDistance;
    double m_ambientTemp;
    double m_initialTemp;
    double m_bodyTemp;
    double m_sensorTemp;
    double m_displayTemp;
    double m_tcSensitivity;
    double m_tcResistance;
    double m_thermalStep;
    double m_lastPower1;
    double m_lastPower2;
    double m_plotWindow;

    uint64_t m_lastThermalTime;
    uint64_t m_thermalPeriod;

    int m_historySize;
    QVector<double> m_historyTime;
    QVector<double> m_historyBody;
    QVector<double> m_historySensor;

    bool m_tcConnected;
    bool m_electricalDirty;
    bool m_rescheduleEvent;
    bool m_guiDirty;
    mutable QMutex m_stateMutex;
};
