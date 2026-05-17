/***************************************************************************
 *   Copyright (C) 2024 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "e-element.h"
#include "iocomponent.h"

class LibraryItem;
class TruthTable;

class TestUnit : public IoComponent, public eElement {
public:
    TestUnit( QString type, QString id );
    ~TestUnit();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    virtual void stamp() override;
    virtual void updateStep() override;
    virtual void runEvent() override;

    QString inputs() { return m_inputStr; }
    void setInputs( QString i );

    QString outputs() { return m_outputStr; }
    void setOutputs( QString o );

    double period() { return m_period; }
    void setPeriod( double p ) { m_period = p; }

    QString truth();
    void setTruth( QString t );

    void save( std::vector<uint> outValues );

    void runTest();

    void loadTest();

protected:
    void contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) override;

private:
    void createTable();
    void resizeVectors();

    double m_period;

    bool m_read;
    int m_steps;

    std::vector<uint> m_samples;
    std::vector<uint> m_truthT;

    //QString m_test;
    QString m_inputStr;
    QString m_outputStr;

    TruthTable* m_truthTable;
};
