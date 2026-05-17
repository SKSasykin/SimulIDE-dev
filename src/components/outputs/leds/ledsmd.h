/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "ledbase.h"

class LedSmd : public LedBase {
public:
    LedSmd( QString type, QString id, QRectF area, ePin* pin0 = NULL, ePin* pin1 = NULL );
    ~LedSmd();

protected:
    void drawBackground( QPainter* p );
    void drawForeground( QPainter* p );
};
