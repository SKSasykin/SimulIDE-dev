/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QSlider>

class CustomSlider : public QSlider {
    Q_OBJECT
public:
    CustomSlider( QWidget* parent = nullptr );

private:
    virtual void paintEvent( QPaintEvent* ) override;
};
