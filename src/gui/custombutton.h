/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QToolButton>

class CustomButton : public QToolButton {
    Q_OBJECT
public:
    CustomButton( QWidget* parent = nullptr );

private:
    virtual void paintEvent( QPaintEvent* ) override;
};
