/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

class Updatable {
public:
    Updatable();
    ~Updatable();

    virtual void updateStep() { ; }
};
