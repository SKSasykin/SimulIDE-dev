/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "connbase.h"

class LibraryItem;

class Socket : public ConnBase {
public:
    Socket( QString type, QString id );
    ~Socket();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void updateStep() override;

    void updatePins() override { connectPins( false ); }
    void connectPins( bool connect );

private:
    void updatePixmap() override;
};
