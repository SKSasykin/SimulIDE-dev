/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <algorithm>

#include <QPainter>

#include "circuit.h"
#include "circuitwidget.h"
#include "iopin.h"
#include "oledcontroller.h"
#include "simulator.h"

#include "boolprop.h"
#include "doubleprop.h"
#include "intprop.h"
#include "stringprop.h"

static constexpr uint64_t DISPLAY_SETTLE_TIME = 200ULL * 1000000000ULL; // 200 ms in ps

OledController::OledController( QString type, QString id ) : Component( type, id ), TwiModule( id ) {
    m_graphical = true;

    m_rotate = false;
    m_displayDirty = true;
    m_pendingWriteCount = 0;
    m_lastBusUpdate = 0;

    m_pin.resize( 2 );
    m_pin[0] = m_clkPin = new IoPin( 270, QPoint( -48, 48 ), id + "-PinSck", 0, this, openCo );
    m_clkPin->setLabelText( "SCL" );
    TwiModule::setSclPin( m_clkPin );

    m_pin[1] = m_pinSda = new IoPin( 270, QPoint( -40, 48 ), id + "-PinSda", 0, this, openCo );
    m_pinSda->setLabelText( "SDA" );
    TwiModule::setSdaPin( m_pinSda );

    Simulator::self()->addToUpdateList( this );

    setShowId( true );

    reset();
}
OledController::~OledController() { }

void OledController::initialize() {
    TwiModule::initialize();

    clearDDRAM();
    reset();
    OledController::updateStep();
}

void OledController::stamp() {
    setMode( TWI_SLAVE );
}

void OledController::updateStep() {
    uint64_t circTime = Simulator::self()->circTime();
    bool frameComplete = m_pendingWriteCount == m_width * m_rows;
    bool busSettled = !m_lastBusUpdate || circTime - m_lastBusUpdate >= DISPLAY_SETTLE_TIME;
    if ( m_displayDirty && ( frameComplete || busSettled ) ) {
        renderDisplay();
        for ( std::vector<bool>& column : m_pendingWrites )
            std::fill( column.begin(), column.end(), false );
        m_pendingWriteCount = 0;
    }
    update();
    if ( !m_scrollSingle && !m_scroll )
        return;
    if ( Simulator::self()->isPaused() )
        return;

    if ( m_scrollSingle ) {
        m_scrollSingle = false;
    } else {
        m_scrollCount++;
        if ( m_scrollCount < m_scrollStep )
            return;
        m_scrollCount = 0;
    }

    int maxX = m_width - 1;
    bool scrollRight = false;
    if ( m_scrollV )
        scrollRight = m_scrollDir == 1;
    else
        scrollRight = m_scrollDir == 2;

    for ( int row = m_scrollStartY; row <= m_scrollEndY; row++ ) {
        int dy = row;

        if ( scrollRight ) {
            uint8_t end = m_DDRAM[maxX][dy];
            for ( int col = maxX; col > 0; --col )
                m_DDRAM[col][dy] = m_DDRAM[col - 1][dy];
            m_DDRAM[0][dy] = end;
        } else {
            uint8_t start = m_DDRAM[0][dy];
            for ( int col = 0; col < maxX; ++col )
                m_DDRAM[col][dy] = m_DDRAM[col + 1][dy];
            m_DDRAM[maxX][dy] = start;
        }
    }
    m_displayDirty = true;
    if ( !m_scrollV )
        return;

    for ( int col = 0; col < maxX; ++col ) {
        uint64_t ramCol = 0;

        for ( int row = m_scrollEndY; row >= m_scrollStartY; row-- ) {
            ramCol <<= 8;
            ramCol |= m_DDRAM[col][row];
        }
        if ( ramCol == 0 )
            continue;

        uint8_t nBits = ( m_scrollEndY - m_scrollStartY + 1 ) * 8;
        uint64_t mask = ( 1ULL << m_vScrollOffset ) - 1;

        uint64_t upper = ( ramCol & mask ) << ( nBits - m_vScrollOffset );
        uint64_t lower = ramCol >> m_vScrollOffset;
        ramCol = upper | lower;

        for ( int row = m_scrollStartY; row <= m_scrollEndY; row++ ) {
            m_DDRAM[col][row] = ramCol & 0xFF;
            ramCol >>= 8;
        }
    }
}

void OledController::reset() {
    //m_cdr  = 1;
    m_mr = 63;
    //m_fosc = 370000;
    //m_frm  = m_fosc/(m_cdr*54*m_mr);

    m_addrX = 0;
    m_addrY = 0;
    m_startX = 0;
    m_endX = m_width - 1;
    m_startY = 0;
    m_endY = m_rows - 1;

    m_scroll = false;
    m_scrollV = false;
    m_scrollDir = false;
    m_scrollSingle = false;
    m_scrollStartY = 0;
    m_scrollEndY = 7;
    m_scrollStep = 5;
    m_vScrollOffset = 0;

    m_dispOffset = 0;
    m_ramOffset = 0;
    m_readBytes = 0;

    m_dispOn = false;
    m_dispFull = false;
    m_dispInv = false;
    m_scanInv = false;
    m_remap = false;

    m_addrMode = PAGE_ADDR_MODE;
    m_displayDirty = true;
    for ( std::vector<bool>& column : m_pendingWrites )
        std::fill( column.begin(), column.end(), false );
    m_pendingWriteCount = 0;
    m_lastBusUpdate = 0;
}

void OledController::startWrite() {
    m_start = 1;
}

void OledController::readByte() {
    TwiModule::readByte();

    if ( m_start ) // Read Control byte
    {
        m_start = 0;
        if ( ( m_rxReg & 0b00111111 ) != 0 ) {
            qDebug() << "OledController::readByte Control Byte Error";
            //return;
        }
        m_Co = m_rxReg & 0b10000000;
        m_data = m_rxReg & 0b01000000;
    } else if ( m_data )
        writeData();
    else if ( m_readBytes )
        parameter();
    else
        proccessCommand();

    if ( !m_readBytes )
        m_start = m_Co; // If Co bit then next byte should be Control Byte
    m_displayDirty = true;
    m_lastBusUpdate = Simulator::self()->circTime();
}

void OledController::writeData() {
    if ( !m_pendingWrites[m_addrX][m_addrY] ) {
        m_pendingWrites[m_addrX][m_addrY] = true;
        m_pendingWriteCount++;
    }
    m_DDRAM[m_addrX][m_addrY] = m_rxReg;

    if ( m_addrMode & VERT_ADDR_MODE ) {
        m_addrY++;
        if ( m_addrY > m_endY ) {
            m_addrY = m_startY;
            if ( m_addrMode != VERT_ADDR_MODE )
                return;
            m_addrX++;
            if ( m_addrX > m_endX )
                m_addrX = m_startX;
        }
    } else {
        m_addrX++;
        if ( m_addrX > m_endX ) {
            m_addrX = m_startX;
            if ( m_addrMode != HORI_ADDR_MODE )
                return;
            m_addrY++;
            if ( m_addrY > m_endY )
                m_addrY = m_startY;
        }
    }
}

void OledController::clearDDRAM() {
    for ( int col = 0; col < m_width; col++ )
        for ( int row = 0; row < m_rows; row++ )
            m_DDRAM[col][row] = 0;
    m_displayDirty = true;
}

void OledController::setColorStr( QString color ) {
    m_dColor = color;

    if ( color == "White" )
        m_foreground = QColor( 245, 245, 245 );
    if ( color == "Blue" )
        m_foreground = QColor( 200, 200, 255 );
    if ( color == "Yellow" )
        m_foreground = QColor( 245, 245, 100 );

    if ( m_showVal && ( m_showProperty == "Color" ) )
        setValLabelText( color );
    m_displayDirty = true;
}

void OledController::setWidth( int w ) {
    if ( w > m_maxWidth )
        w = m_maxWidth;
    else if ( w < 32 )
        w = 32;
    if ( m_width == w )
        return;
    m_width = w;
    m_displayDirty = true;
    updateSize();
}

void OledController::setHeight( int h ) {
    if ( h > m_height )
        h += 8;
    if ( h > m_maxHeight )
        h = m_maxHeight;
    else if ( h < 16 )
        h = 16;

    h = ( h / 8 ) * 8;
    if ( m_height == h )
        return;

    m_rows = h / 8;
    m_lineMask = ( h > 64 ) ? 0x7F : 0x3F;
    m_rowMask = ( h > 64 ) ? 0x0F : 0x07;
    m_height = h;
    m_displayDirty = true;
    updateSize();
}

void OledController::setSize( int w, int h ) {
    if ( Simulator::self()->isRunning() )
        CircuitWidget::self()->powerCircOff();

    m_maxWidth = w;
    m_maxHeight = h;
    setWidth( w );
    setHeight( h );

    m_DDRAM.resize( m_width, std::vector<uint8_t>( m_rows, 0 ) );
    m_pendingWrites.resize( m_width, std::vector<bool>( m_rows, false ) );
    m_pendingWriteCount = 0;
    m_displayDirty = true;
}

void OledController::updateSize() {
    m_area = QRectF( -70, -m_height / 2 - 16, m_width + 12, m_height + 24 );

    m_clkPin->setPos( QPoint( -48, m_height / 2 + 16 ) );
    m_clkPin->isMoved();
    m_pinSda->setPos( QPoint( -40, m_height / 2 + 16 ) );
    m_pinSda->isMoved();

    Circuit::self()->update();
}

void OledController::renderDisplay() {
    m_displayImage = QImage( m_width, m_height, QImage::Format_RGB32 );
    m_displayImage.fill( Qt::black );

    QRgb foreground = m_foreground.rgb();
    for ( int col = 0; col < m_width; col++ ) {
        for ( int row = 0; row < m_rows; row++ ) {
            int ramY = row * 8;
            if ( m_ramOffset ) {
                ramY += m_ramOffset;
                if ( ramY >= m_height )
                    ramY -= m_height;
            }
            if ( ramY > m_mr )
                continue;

            uint8_t rowByte = ramY / 8;
            uint8_t byte0 = m_DDRAM[col][rowByte];
            if ( m_dispInv )
                byte0 = ~byte0;

            uint8_t startBit = ramY % 8;
            uint8_t byte1 = 0;
            if ( startBit ) {
                rowByte = ( rowByte + 1 ) % m_rows;
                byte1 = m_DDRAM[col][rowByte];
                if ( m_dispInv )
                    byte1 = ~byte1;
            }
            int dy = row * 8;
            if ( m_dispOffset ) {
                dy += m_dispOffset;
                if ( dy >= m_height )
                    dy -= m_height;
            }

            for ( int bit = startBit; bit < startBit + 8; bit++ ) {
                uint8_t pixel = bit < 8 ? byte0 & 1 << bit : byte1 & 1 << ( bit - 8 );
                if ( pixel ) {
                    int screenY = m_scanInv ? m_height - 1 - dy : dy;
                    int screenX = m_remap ? m_width - 1 - col : col;
                    if ( m_rotate ) {
                        screenY = m_height - 1 - screenY;
                        screenX = m_width - 1 - screenX;
                    }
                    reinterpret_cast<QRgb*>( m_displayImage.scanLine( screenY ) )[screenX] = foreground;
                }
                dy++;
                if ( dy >= m_height )
                    dy -= m_height;
            }
        }
    }
    m_displayDirty = false;
}

void OledController::paint( QPainter* p, const QStyleOptionGraphicsItem*, QWidget* ) {
    QPen pen( Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
    p->setPen( pen );

    p->setBrush( QColor( 50, 70, 100 ) );
    p->drawRoundedRect( m_area, 2, 2 );

    if ( !m_dispOn )
        p->fillRect( -64, -m_height / 2 - 10, m_width, m_height, Qt::black );
    else if ( m_dispFull )
        p->fillRect( -64, -m_height / 2 - 10, m_width, m_height, m_foreground );
    else
        p->drawImage( QRectF( -64, -m_height / 2 - 10, m_width, m_height ), m_displayImage );
    Component::paintSelected( p );
}
