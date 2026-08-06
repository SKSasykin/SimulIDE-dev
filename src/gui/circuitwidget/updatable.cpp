/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "updatable.h"
#include "simulator.h"

Updatable::Updatable() { }
Updatable::~Updatable() {
    if ( Simulator::self() )
        Simulator::self()->remFromUpdateList( this );
}
