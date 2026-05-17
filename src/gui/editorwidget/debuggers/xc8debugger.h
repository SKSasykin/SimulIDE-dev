/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "avrgccdebugger.h"

class Xc8Debugger : public AvrGccDebugger {
public:
    Xc8Debugger( CodeEditor* parent, OutPanelText* outPane );
    ~Xc8Debugger();
};
