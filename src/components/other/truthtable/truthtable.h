/***************************************************************************
 *   Copyright (C) 2024 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QDialog>

#include "ui_truthtable.h"

class TestUnit;

class TruthTable : public QDialog, private Ui::TruthTable {
    Q_OBJECT

public:
    TruthTable( TestUnit* tu, QWidget* parent = 0 );

    void setup( QString inputs, QString outputs, std::vector<uint> truthT );

    bool checkThruth( std::vector<uint>* samples );

public slots:
    void on_saveButton_pressed();
    void on_runButton_pressed();
    void on_clearButton_pressed();
    void on_table_itemDoubleClicked( QTableWidgetItem* item );

private:
    std::vector<uint> getTruthVector();
    void setItemColor( QTableWidgetItem* it, bool b );

    TestUnit* m_testUnit;

    QString m_inputsStr;
    QString m_outputsStr;

    int m_numInputs;
    int m_numOutputs;

    std::vector<uint> m_truthVector;
};
