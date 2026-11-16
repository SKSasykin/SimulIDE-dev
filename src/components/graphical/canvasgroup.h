#pragma once

#include "component.h"

class CanvasGroup : public Component {
public:
    CanvasGroup( QString type, QString id );
    ~CanvasGroup();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

    QString membersStr();
    void setMembersStr( QString members );
    QString title() { return m_title; }
    void setTitle( QString title );
    QString colorStr() { return m_color.name(); }
    void setColorStr( QString color );
    QString backgroundColorStr() { return m_backgroundColor.name(); }
    void setBackgroundColorStr( QString color );
    double backgroundOpacity() { return m_backgroundOpacity; }
    void setBackgroundOpacity( double opacity );

    void setMembers( const QList<Component*>& members );
    QList<Component*> members() const { return m_members; }
    void resolveMembers();
    void removeMember( Component* comp );
    void detachMembers();
    bool hasMember( Component* comp ) const { return m_members.contains( comp ); }
    void refreshFrame();

    void setPosition( QPointF pos ) override;
    void move( QPointF delta ) override;
    void remove() override;
    bool freeMove( bool ) override { return true; }

    void paint( QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;
    void contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) override;
    QVariant itemChange( GraphicsItemChange change, const QVariant& value ) override;

private:
    QRectF titleRect() const;

    QString m_title;
    QColor m_backgroundColor;
    double m_backgroundOpacity;
    QStringList m_memberIds;
    QList<Component*> m_members;
};
