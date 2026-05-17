/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "varresbase.h"

class ThermistorBase : public VarResBase {
public:
    ThermistorBase( QString type, QString id );
    ~ThermistorBase();

    bool ptc() { return m_ptc; }
    void setPtc( bool ptc );

    virtual void setMaxVal( double max ) override;
    virtual void setMinVal( double min ) override;

protected:
    bool m_ptc;
};
