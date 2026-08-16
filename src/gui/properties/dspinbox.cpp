/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <math.h>

#include "dspinbox.h"

asIScriptEngine* DSpinBox::m_aEngine = nullptr;

DSpinBox::DSpinBox( QWidget* parent ) : QDoubleSpinBox( parent ) {
    if ( !m_aEngine )
        m_aEngine = asCreateScriptEngine();
}
DSpinBox::~DSpinBox() {
    ;
}

QString DSpinBox::textFromValue( double value ) const {
    return QString::number( value );
}

QValidator::State DSpinBox::validate( QString& text, int& ) const {
    return isnan( valueFromText( text ) ) ? QValidator::Invalid : QValidator::Acceptable;
}

double DSpinBox::valueFromText( const QString& text ) const {
    QString exp = text;
    if ( exp.indexOf( "e" ) == exp.size() - 1 )
        exp.append( "0" );
    if ( exp.indexOf( "+" ) == exp.size() - 1 )
        exp.append( "0" );
    if ( exp.indexOf( "-" ) == exp.size() - 1 )
        exp.append( "0" );
    if ( exp.indexOf( "*" ) == exp.size() - 1 )
        exp.append( "1" );
    if ( exp.indexOf( "/" ) == exp.size() - 1 )
        exp.append( "1" );

    return evaluate( exp );
}

double DSpinBox::evaluate( QString exp ) {
    exp = "return " + exp;
    QByteArray ba = exp.toLocal8Bit();
    const char* code = ba.constData();
    double res = 0;
    int r = ExecuteString( m_aEngine, code, &res, asTYPEID_DOUBLE );
    if ( r >= 0 )
        return res;
    return NAN;
}
