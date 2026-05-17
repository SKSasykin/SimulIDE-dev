/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcuwdt.h"

class PicWdt : public McuWdt {
public:
    PicWdt( eMcu* mcu, QString name );
    ~PicWdt();

    static PicWdt* createWdt( eMcu* mcu, QString name, int type );

    virtual void initialize() override;
    virtual void runEvent() override;

    virtual void reset() override;

    virtual void sleep( int mode ) override;

protected:
    bool m_wdtSoft;
};

class PicWdt00 : public PicWdt {
public:
    PicWdt00( eMcu* mcu, QString name );
    ~PicWdt00();

    void setup() override;

    void configureA( uint8_t newOPTION ) override;

private:
    regBits_t m_PS;
    regBits_t m_PSA;
};

class PicWdt01 : public PicWdt {
public:
    PicWdt01( eMcu* mcu, QString name );
    ~PicWdt01();

    virtual void setup() override;

    void configureA( uint8_t newWDTCON ) override;

private:
    regBits_t m_SWDTEN;
    regBits_t m_WDTPS;
};
