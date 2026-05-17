/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "cdebugger.h"

class AvrGccDebugger : public cDebugger {
public:
    AvrGccDebugger( CodeEditor* parent, OutPanelText* outPane );
    ~AvrGccDebugger();

protected:
    virtual bool postProcess() override;

    bool getVariables();
    bool getFunctions();
    bool mapFlashToSource();

    int m_addrBytes;

    QString m_elfPath;
};
