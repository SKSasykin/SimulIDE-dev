/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QColorDialog>

#include "colorval.h"
#include "component.h"
#include "comproperty.h"
#include "propdialog.h"

ColorVal::ColorVal( PropDialog* parent, CompBase* comp, ComProperty* prop ) : PropVal( parent, comp, prop ) {
    setupUi( this );
    m_blocked = true;
}
ColorVal::~ColorVal() { }

void ColorVal::setup( bool ) {
    //QFontMetrics fm( colorW->font() );
    //float scale = fm.width(" ")/2;
    //colorW->setFixedWidth( 170.0*scale );

    valLabel->setText( m_property->capt() );

    QString colorStr = m_property->getValStr();
    m_color = QColor( colorStr );

    QPalette pal = colorW->palette();
    pal.setColor( QPalette::Base, m_color );
    colorW->setPalette( pal );
    colorW->setText( colorStr );
    colorW->setReadOnly( true );
    colorW->installEventFilter( this );

    this->adjustSize();

    m_blocked = false;
}

bool ColorVal::eventFilter( QObject* object, QEvent* event ) {
    if ( object == colorW && event->type() == QEvent::MouseButtonPress ) {
        changeColor();
    }
    return false;
}

void ColorVal::changeColor() {
    if ( m_blocked )
        return;
    if ( !m_component )
        return;

    QColor color = QColorDialog::getColor( m_color, this, tr( "Select Color" ), QColorDialog::DontUseNativeDialog );
    if ( !color.isValid() )
        return;
    m_color = color;

    QString colorStr = m_color.name();
    m_component->setPropStr( m_propName, colorStr );

    QPalette pal = colorW->palette();
    pal.setColor( QPalette::Base, m_color );
    colorW->setPalette( pal );
    colorW->setText( colorStr );

    m_propDialog->changed();
}

void ColorVal::updtValues() { }
