/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QScrollBar>

class CodeEditor;

class scrollWidget : public QScrollBar {
public:
    scrollWidget( CodeEditor* editor, Qt::Orientation o, QWidget* parent = 0 );
    ~scrollWidget();

    void paintEvent( QPaintEvent* event ) override;

    CodeEditor* m_editor;
};
