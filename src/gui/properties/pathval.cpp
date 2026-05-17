/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QFileDialog>

#include "component.h"
#include "comproperty.h"
#include "mainwindow.h"
#include "pathval.h"
#include "propdialog.h"
#include "utils.h"

PathVal::PathVal( PropDialog* parent, CompBase* comp, ComProperty* prop, bool isFile ) : PropVal( parent, comp, prop ) {
    setupUi( this );

    m_isFile = isFile;
}
PathVal::~PathVal() { }

void PathVal::setup( bool ) {
    valLabel->setText( m_property->capt() );
    updtValues();
    this->adjustSize();
}

void PathVal::on_value_editingFinished() {
    QString path = value->text();

    if ( !m_isFile && !path.isEmpty() && !path.endsWith( "/" ) && !path.endsWith( "\\" ) )
        path.append( "/" );
    m_property->setValStr( path );
    updtValues();
    m_propDialog->changed();
}

void PathVal::on_setPathButton_clicked() {
    QString path;
    if ( m_isFile )
        path = QFileDialog::getOpenFileName( 0l, tr( "Select file" ), value->text() );
    else
        path = getDirDialog( tr( "Select directory" ), value->text() );

    value->setText( path );
    on_value_editingFinished();
}

void PathVal::updtValues() {
    QString text = m_property->getValStr();
    value->setText( text );
}
