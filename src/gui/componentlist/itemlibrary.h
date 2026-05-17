/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"

class LibraryItem;

class ItemLibrary {
public:
    ItemLibrary();
    ~ItemLibrary();

    static ItemLibrary* self() { return m_pSelf; }

    QList<LibraryItem*> items() { return m_items; }
    Component* createItem( QString type, QString id );

    void addItem( LibraryItem* item );

    void loadItems();

protected:
    static ItemLibrary* m_pSelf;

    QList<LibraryItem*> m_items;
};

typedef Component* ( *createItemPtr )( QString type, QString id );
class LibraryItem {
public:
    LibraryItem( QString name, QString category, QString iconName, QString type, createItemPtr createItem );

    ~LibraryItem();

    QString name() { return m_name; }
    QString category() { return m_category; }
    QString iconfile() { return m_iconfile; }
    QString type() { return m_type; }
    bool isType( QString type );

    createItemPtr createItemFnPtr() { return m_createItem; }

private:
    QString m_name;
    QString m_category;
    QString m_iconfile;
    QString m_type;
    QStringList m_typeList;

    createItemPtr m_createItem;
};
