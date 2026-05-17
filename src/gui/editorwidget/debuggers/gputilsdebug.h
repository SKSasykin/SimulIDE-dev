/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

class BaseDebugger;

class GputilsDebug {
public:
    GputilsDebug();
    ~GputilsDebug();

    //int compile( bool debug );

    static bool getVariables( BaseDebugger* debugger );
    static bool mapFlashToSource( BaseDebugger* debugger );
    //static bool mapFlashToAsm( BaseDebugger* debugger );
};
