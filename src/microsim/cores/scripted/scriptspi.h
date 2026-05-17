/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcuspi.h"
#include "scriptperif.h"

class ScriptCpu;
class asIScriptFunction;

class ScriptSpi : public McuSpi, public ScriptPerif {
public:
    ScriptSpi( eMcu* mcu, QString name );
    ~ScriptSpi();

    void byteReceived( uint8_t data );
    void sendByte( uint8_t data );

    void reset() override;

    void endTransaction() override;

    QStringList registerScript( ScriptCpu* cpu ) override;
    void startScript() override;

private:
    asIScriptFunction* m_byteReceived;
};
