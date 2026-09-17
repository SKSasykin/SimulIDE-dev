#include "blechatformat.h"

static inline bool bleIsHexDigit( QChar c )
{
    return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );
}

static inline int bleHexValue( QChar c )
{
    if ( c >= '0' && c <= '9' ) return c.unicode() - '0';
    if ( c >= 'a' && c <= 'f' ) return c.unicode() - 'a' + 10;
    return c.unicode() - 'A' + 10;
}

QString bleChatFormatString( const QByteArray& data )
{
    QString result;
    result.reserve( data.size() );
    const char hex[] = "0123456789ABCDEF";
    for ( int i = 0; i < data.size(); ++i ) {
        uint8_t byte = static_cast<uint8_t>( data.at( i ) );
        if ( byte == '\\' ) {
            result += "\\\\";
        } else if ( byte >= 0x20 && byte <= 0x7E ) {
            result += QChar( byte );
        } else {
            result += "\\x";
            result += QChar( hex[( byte >> 4 ) & 0xF] );
            result += QChar( hex[byte & 0xF] );
        }
    }
    return result;
}

QString bleChatFormatHex( const QByteArray& data )
{
    QString result;
    const char hex[] = "0123456789ABCDEF";
    for ( int i = 0; i < data.size(); ++i ) {
        if ( i > 0 ) result += ' ';
        uint8_t byte = static_cast<uint8_t>( data.at( i ) );
        result += QChar( hex[( byte >> 4 ) & 0xF] );
        result += QChar( hex[byte & 0xF] );
    }
    return result;
}

bool bleChatParseString( const QString& text, QByteArray& out, QString& error )
{
    out.clear();
    QString plain;
    auto flushPlain = [&]() {
        if ( !plain.isEmpty() ) {
            out += plain.toUtf8();
            plain.clear();
        }
    };
    int i = 0;
    const int n = text.size();
    while ( i < n ) {
        QChar c = text.at( i );
        if ( c == '\\' ) {
            if ( i + 1 >= n ) {
                error = "trailing backslash";
                return false;
            }
            QChar next = text.at( i + 1 );
            if ( next == '\\' ) {
                flushPlain();
                out.append( '\\' );
                i += 2;
            } else if ( next == 'x' || next == 'X' ) {
                if ( i + 3 >= n || !bleIsHexDigit( text.at( i + 2 ) ) || !bleIsHexDigit( text.at( i + 3 ) ) ) {
                    error = "bad \\x escape, expected \\xNN";
                    return false;
                }
                flushPlain();
                int value = ( bleHexValue( text.at( i + 2 ) ) << 4 ) | bleHexValue( text.at( i + 3 ) );
                out.append( static_cast<char>( value ) );
                i += 4;
            } else {
                error = "unsupported escape";
                return false;
            }
        } else {
            plain += c;
            ++i;
        }
    }
    flushPlain();
    return true;
}

bool bleChatParseHex( const QString& text, QByteArray& out, QString& error )
{
    out.clear();
    QString compact;
    compact.reserve( text.size() );
    for ( int i = 0; i < text.size(); ++i ) {
        QChar c = text.at( i );
        if ( c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == '-' || c == ':' )
            continue;
        if ( c == 'x' || c == 'X' ) {
            if ( !compact.isEmpty() && compact.at( compact.size() - 1 ) == '0' ) {
                compact.chop( 1 );
                continue;
            }
            error = "unexpected x without 0 prefix";
            return false;
        }
        if ( !bleIsHexDigit( c ) ) {
            error = "invalid hex character";
            return false;
        }
        compact += c;
    }
    if ( compact.isEmpty() )
        return true;
    if ( compact.size() % 2 != 0 ) {
        error = "odd hex digit count";
        return false;
    }
    out.reserve( compact.size() / 2 );
    for ( int i = 0; i < compact.size(); i += 2 ) {
        int value = ( bleHexValue( compact.at( i ) ) << 4 ) | bleHexValue( compact.at( i + 1 ) );
        out.append( static_cast<char>( value ) );
    }
    return true;
}
