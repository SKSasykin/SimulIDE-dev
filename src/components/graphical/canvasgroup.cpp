#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QPainterPathStroker>

#include "canvasgroup.h"
#include "circuit.h"
#include "connector.h"
#include "doubleprop.h"
#include "pin.h"
#include "stringprop.h"

#define tr( str ) simulideTr( "CanvasGroup", str )

CanvasGroup::CanvasGroup( QString type, QString id ) : Component( type, id ) {
    m_graphical = true;
    m_color = QColor( Qt::darkGray );
    m_backgroundColor = QColor( Qt::lightGray );
    m_backgroundOpacity = 0.12;
    m_title = tr( "Group" );
    m_area = QRectF( -20, -20, 40, 40 );
    setZValue( -0.5 );

    addPropGroup(
        { tr( "Main" ),
          { new StrProp<CanvasGroup>( "Title", tr( "Label" ), "", this, &CanvasGroup::title,
                                      &CanvasGroup::setTitle ),
            new StrProp<CanvasGroup>( "Color", tr( "Frame Color" ), "", this, &CanvasGroup::colorStr,
                                      &CanvasGroup::setColorStr, 0, "color" ),
            new StrProp<CanvasGroup>( "Background", tr( "Background Color" ), "", this,
                                      &CanvasGroup::backgroundColorStr, &CanvasGroup::setBackgroundColorStr, 0,
                                      "color" ),
            new DoubProp<CanvasGroup>( "Opacity", tr( "Background Opacity" ), "", this,
                                       &CanvasGroup::backgroundOpacity, &CanvasGroup::setBackgroundOpacity ) },
          0 } );

    addPropGroup(
        { "CanvasGroup",
          { new StrProp<CanvasGroup>( "Members", "", "", this, &CanvasGroup::membersStr,
                                      &CanvasGroup::setMembersStr ) },
          groupHidden } );
}

CanvasGroup::~CanvasGroup() {
    detachMembers();
}

QRectF CanvasGroup::boundingRect() const {
    return m_area.adjusted( -2, -2, 2, 2 ).united( titleRect() );
}

QPainterPath CanvasGroup::shape() const {
    QPainterPath path;
    path.addRect( m_area );
    QPainterPathStroker stroker;
    stroker.setWidth( 8 );
    path = stroker.createStroke( path );
    path.addRect( titleRect() );
    return path;
}

QString CanvasGroup::membersStr() {
    return m_memberIds.join( "," );
}

void CanvasGroup::setMembersStr( QString members ) {
    m_memberIds.clear();
    for ( QString id : members.split( ",", Qt::SkipEmptyParts ) ) {
        if ( Circuit::self()->pasting() )
            id = Circuit::self()->replaceId( id );
        if ( !m_memberIds.contains( id ) )
            m_memberIds.append( id );
    }
}

void CanvasGroup::setTitle( QString title ) {
    prepareGeometryChange();
    m_title = title;
    update();
}

void CanvasGroup::setColorStr( QString color ) {
    m_color = QColor( color );
    update();
}

void CanvasGroup::setBackgroundColorStr( QString color ) {
    m_backgroundColor = QColor( color );
    update();
}

void CanvasGroup::setBackgroundOpacity( double opacity ) {
    m_backgroundOpacity = qBound( 0.0, opacity, 1.0 );
    update();
}

void CanvasGroup::setMembers( const QList<Component*>& members ) {
    detachMembers();
    m_memberIds.clear();
    for ( Component* comp : members ) {
        if ( !comp || comp == this || comp->canvasGroup() )
            continue;
        m_members.append( comp );
        m_memberIds.append( comp->getUid() );
        comp->setCanvasGroup( this );
    }
    refreshFrame();
}

void CanvasGroup::resolveMembers() {
    QList<Component*> members;
    for ( QString id : m_memberIds ) {
        Component* comp = dynamic_cast<Component*>( Circuit::self()->compMap()->value( id ) );
        if ( comp && comp != this && !comp->canvasGroup() )
            members.append( comp );
    }
    m_members.clear();
    for ( Component* comp : members ) {
        m_members.append( comp );
        comp->setCanvasGroup( this );
    }
    if ( isSelected() )
        for ( Component* comp : m_members )
            comp->setSelected( false );
    refreshFrame();
}

void CanvasGroup::removeMember( Component* comp ) {
    if ( !m_members.removeOne( comp ) )
        return;
    m_memberIds.removeAll( comp->getUid() );
    comp->setCanvasGroup( nullptr );
    refreshFrame();
}

void CanvasGroup::detachMembers() {
    for ( Component* comp : m_members )
        if ( comp && comp->canvasGroup() == this )
            comp->setCanvasGroup( nullptr );
    m_members.clear();
}

void CanvasGroup::refreshFrame() {
    if ( m_members.isEmpty() )
        return;

    QRectF frame;
    for ( Component* comp : m_members )
        frame = frame.isNull() ? comp->sceneBoundingRect() : frame.united( comp->sceneBoundingRect() );
    frame = frame.adjusted( -12, -12, 12, 12 );

    prepareGeometryChange();
    setPos( frame.center() );
    m_area = frame.translated( -frame.center() );
    update();
}

QRectF CanvasGroup::titleRect() const {
    if ( m_title.isEmpty() )
        return QRectF();
    QFontMetricsF metrics( QApplication::font() );
    QRectF rect = metrics.boundingRect( m_title ).adjusted( -4, -1, 4, 1 );
    rect.moveCenter( QPointF( 0, m_area.top() ) );
    return rect;
}

void CanvasGroup::setPosition( QPointF pos ) {
    move( pos - position() );
}

void CanvasGroup::move( QPointF delta ) {
    if ( delta.isNull() )
        return;

    QList<Connector*> internal;
    QList<Connector*> external;
    for ( Component* comp : m_members ) {
        for ( Pin* pin : comp->getPins() ) {
            if ( !pin || !pin->connector() )
                continue;
            Connector* connector = pin->connector();
            Component* start = connector->startPin() ? connector->startPin()->component() : nullptr;
            Component* end = connector->endPin() ? connector->endPin()->component() : nullptr;
            QList<Connector*>& list = hasMember( start ) && hasMember( end ) ? internal : external;
            if ( !list.contains( connector ) )
                list.append( connector );
        }
    }

    for ( Component* comp : m_members )
        comp->setPos( comp->pos() + delta );
    for ( Connector* connector : internal )
        connector->translate( delta );
    for ( Connector* connector : external )
        connector->isMoved();

    setPos( pos() + delta );
}

void CanvasGroup::remove() {
    detachMembers();
    Component::remove();
}

void CanvasGroup::paint( QPainter* painter, const QStyleOptionGraphicsItem*, QWidget* ) {
    QColor background = m_backgroundColor;
    background.setAlphaF( m_backgroundOpacity );
    painter->fillRect( m_area, background );

    painter->setPen( QPen( m_color, 1, Qt::DashLine ) );
    painter->setBrush( Qt::NoBrush );
    painter->drawRect( m_area );

    if ( !m_title.isEmpty() ) {
        painter->setFont( QApplication::font() );
        QRectF textRect = titleRect();
        painter->fillRect( textRect, QColor( 255, 255, 255, 220 ) );
        painter->setPen( m_color );
        painter->drawText( textRect, Qt::AlignCenter, m_title );
    }
    paintSelected( painter );
}

void CanvasGroup::contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) {
    event->accept();
    QAction* ungroupAction = menu->addAction( QIcon( ":/detach.png" ), tr( "Ungroup" ) );
    QObject::connect( ungroupAction, &QAction::triggered, [=]() { Circuit::self()->ungroup( this ); } );
    menu->addSeparator();

    QAction* propertiesAction = menu->addAction( QIcon( ":/properties.svg" ), tr( "Properties" ) );
    QObject::connect( propertiesAction, &QAction::triggered, [=]() { slotProperties(); } );

    QAction* removeAction = menu->addAction( QIcon( ":/remove.svg" ), tr( "Remove group" ) );
    QObject::connect( removeAction, &QAction::triggered, [=]() { slotRemove(); } );
    menu->exec( event->screenPos() );
}

QVariant CanvasGroup::itemChange( GraphicsItemChange change, const QVariant& value ) {
    if ( change == QGraphicsItem::ItemSelectedChange && value.toBool() )
        for ( Component* comp : m_members )
            comp->setSelected( false );
    return Component::itemChange( change, value );
}
