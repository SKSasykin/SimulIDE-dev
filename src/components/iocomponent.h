/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <queue>

#include "component.h"
#include "logicfamily.h"

class eElement;
class IoPin;

class IoComponent : public Component, public LogicFamily {
public:
    IoComponent( QString type, QString id );
    ~IoComponent();

    QList<ComProperty*> familyProps();
    QList<ComProperty*> inputProps();
    QList<ComProperty*> outputProps();
    QList<ComProperty*> outputType();
    QList<ComProperty*> edgeProps();

    void initState();
    void runOutputs();
    void scheduleOutPuts( eElement* el );

    void updtProperties();

    void setSupplyV( double v ) override;
    void setInpHighV( double volt ) override;
    void setInpLowV( double volt ) override;
    void setOutHighV( double volt ) override;
    void setOutLowV( double volt ) override;
    void setInputImp( double imp ) override;
    void setOutImp( double imp ) override;
    void setRiseTime( double time ) override;
    void setFallTime( double time ) override;
    void setInpPullups( double p ) override;
    void setOutPullups( double p ) override;

    double openImp() { return m_openImp; }
    void setOpenImp( double imp );

    bool invertOuts() { return m_invOutputs; }
    void setInvertOuts( bool invert );

    bool invertInps() { return m_invInputs; }
    virtual void setInvertInps( bool invert );

    int numInps() { return m_inpPin.size(); }
    virtual void setNumInps( uint pins, QString label = "I", int bit0 = 0, int id0 = 0 );

    int numOuts() { return m_outPin.size(); }
    virtual void setNumOuts( uint pins, QString label = "O", int bit0 = 0, int id0 = 0 );

    bool openCol() { return m_openCol; }
    void setOpenCol( bool op );

    void init( QStringList pins );
    void initPin( IoPin* pin );

    std::vector<Pin*> getPins() override;

    void remove() override;

    void setHidden( bool hid, bool hidArea = false, bool hidLabel = false ) override;

protected:
    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
    void slotProperties() override;

    IoPin* createPin( QString data, QString id );
    void setupPin( IoPin* pin, QString data );
    void setNumPins( std::vector<IoPin*>* pinList, uint pins, QString label, int bit0, bool out, int id0 );
    void deletePins( std::vector<IoPin*>* pinList, int pins );

    virtual void updtOutPins();
    virtual void updtInPins();

    uint m_outValue;
    uint m_nextOutVal;
    std::queue<uint> m_outQueue;
    std::queue<uint64_t> m_timeQueue;

    bool m_openCol;
    bool m_invOutputs;
    bool m_invInputs;

    double m_openImp;

    uint m_width;
    uint m_height;

    eElement* m_eElement;

    std::vector<IoPin*> m_inpPin;
    std::vector<IoPin*> m_outPin;
    std::vector<IoPin*> m_otherPin;
};
