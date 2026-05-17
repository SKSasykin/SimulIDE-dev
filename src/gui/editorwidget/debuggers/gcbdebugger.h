/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "basedebugger.h"

class GcbDebugger : public BaseDebugger {
public:
    GcbDebugger( CodeEditor* parent, OutPanelText* outPane );
    ~GcbDebugger();

protected:
    virtual int getErrorLine( QString txt ) override;
    virtual bool postProcess() override;

private:
    bool mapFlashToSource();
    void getSubs();
};
