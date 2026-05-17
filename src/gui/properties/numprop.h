/***************************************************************************
 *   Copyright (C) 2024 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "comproperty.h"

class NumProp : public ComProperty {
public:
    NumProp( QString name, QString caption, QString unit, QString type, uint8_t flags );
    virtual ~NumProp() { ; }

    virtual QString getValStr() override;
    virtual void setValStr( QString ) override;

protected:
    double getVal( const QString& val );
    virtual void setVal( double v ) = 0;

    QString getStr( double val );
};
