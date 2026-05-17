/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "propval.h"
#include "ui_boolval.h"

class Component;
class PropDialog;

class BoolVal : public PropVal, private Ui::BoolVal {
    Q_OBJECT

public:
    BoolVal( PropDialog* parent, CompBase* comp, ComProperty* prop );
    ~BoolVal();

    virtual void setup( bool ) override;
    virtual void updtValues() override;

public slots:
    void on_trueVal_toggled( bool checked );
};
