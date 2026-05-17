/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "propval.h"
#include "ui_pathval.h"

class Component;
class PropDialog;

class PathVal : public PropVal, private Ui::PathVal {
    Q_OBJECT

public:
    PathVal( PropDialog* parent, CompBase* comp, ComProperty* prop, bool isFile );
    ~PathVal();

    virtual void setup( bool ) override;
    virtual void updtValues() override;

public slots:
    void on_value_editingFinished();
    void on_setPathButton_clicked();

private:
    bool m_isFile;
};
