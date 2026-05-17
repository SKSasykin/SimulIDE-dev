/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "cdebugger.h"

class SdccDebugger : public cDebugger {
public:
    SdccDebugger( CodeEditor* parent, OutPanelText* outPane );
    ~SdccDebugger();

    virtual int compile( bool debug ) override;

protected:
    virtual bool postProcess() override;

    bool findCSEG();
};
