/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcusignal.h"
#include <inttypes.h>

struct regInfo_t {
    uint16_t address;
    uint8_t resetVal;
};

struct regBits_t {
    uint8_t bit0 = 0;
    uint8_t mask = 0;
    uint8_t* reg = 0;
    uint16_t regAddr = 0;
};
