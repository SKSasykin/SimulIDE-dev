/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "propval.h"
#include "ui_textval.h"

class PropDialog;
class Scripted;

class TextVal : public PropVal, private Ui::TextVal {
    Q_OBJECT

public:
    TextVal( PropDialog* parent, CompBase* comp, ComProperty* prop );
    ~TextVal();

    virtual void setup( bool isComp ) override;
    virtual void updtValues() override;

public slots:
    //void on_saveButton_clicked();
    void on_textBox_textChanged();

private:
    void updatValue();

    //Scripted* m_scriptComp;
};
