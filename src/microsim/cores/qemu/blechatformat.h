#pragma once

#include <QByteArray>
#include <QString>

QString bleChatFormatString( const QByteArray& data );
QString bleChatFormatHex( const QByteArray& data );
bool bleChatParseString( const QString& text, QByteArray& out, QString& error );
bool bleChatParseHex( const QString& text, QByteArray& out, QString& error );
