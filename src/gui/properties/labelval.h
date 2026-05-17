/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QWidget>

#include "ui_labelval.h"

class LabelVal : public QWidget, private Ui::LabelVal {
    Q_OBJECT

public:
    LabelVal( QWidget* parent );

    void setLabelVal( QString caption );
};
