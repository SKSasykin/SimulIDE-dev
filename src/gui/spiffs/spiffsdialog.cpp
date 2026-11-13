#include "spiffsdialog.h"

#include "qemudevice.h"

#include <QButtonGroup>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

QString sizeText( quint64 size ) {
    if ( size < 1024 )
        return QObject::tr( "%1 B" ).arg( size );
    if ( size < 1024 * 1024 )
        return QObject::tr( "%1 KiB" ).arg( size / 1024.0, 0, 'f', 1 );
    return QObject::tr( "%1 MiB" ).arg( size / ( 1024.0 * 1024.0 ), 0, 'f', 1 );
}

bool isTextPath( const QString& path ) {
    static const QSet<QString> extensions = {
        "txt",  "text", "csv",  "tsv",   "json", "xml",  "html", "htm",  "css",  "js",
        "ts",   "md",   "markdown", "log", "ini", "cfg", "conf", "yaml", "yml",  "toml",
        "c",    "h",    "cpp",  "cxx",   "cc",   "hpp",  "hxx",  "py",   "ino",  "sh",
        "bat",  "cmd",  "properties", "sql", "svg", "tex", "rst",  "java", "go",   "rs",
        "lua",  "php",  "rb",   "pl",    "awk",  "diff", "patch", "gitignore", "env",
        "pem",  "cfg",  "srt",  "vtt"
    };
    QString suffix = QFileInfo( path ).suffix().toLower();
    if ( extensions.contains( suffix ) )
        return true;

    QString name = QFileInfo( path ).fileName().toUpper();
    return name == "README" || name == "LICENSE" || name == "COPYING" || name == "CHANGELOG"
           || name == "MAKEFILE" || name == "CMAKE" || name == ".GITIGNORE" || name == ".ENV";
}

QString hexDump( const QByteArray& data ) {
    const int limit = qMin( data.size(), 1024 * 1024 );
    QString out;
    out.reserve( limit / 16 * 80 );

    for ( int offset = 0; offset < limit; offset += 16 ) {
        out += QString( "%1  " ).arg( offset, 8, 16, QChar( '0' ) );
        int count = qMin( 16, limit - offset );
        for ( int i = 0; i < 16; ++i ) {
            if ( i == 8 )
                out += ' ';
            if ( i < count )
                out += QString( "%1 " ).arg( quint8( data.at( offset + i ) ), 2, 16, QChar( '0' ) );
            else
                out += "   ";
        }
        out += " |";
        for ( int i = 0; i < count; ++i ) {
            uchar c = uchar( data.at( offset + i ) );
            out += QChar( c >= 0x20 && c < 0x7f ? c : '.' );
        }
        out += "|\n";
    }
    return out;
}

} // namespace

SpiffsHexEditor::SpiffsHexEditor( QWidget* parent )
    : QPlainTextEdit( parent ) {}

void SpiffsHexEditor::setHexData( const QByteArray& data ) {
    m_data = data;
    setPlainText( hexDump( m_data ) );
}

void SpiffsHexEditor::updateData( int cursorPosition ) {
    setPlainText( hexDump( m_data ) );
    QTextCursor cursor = textCursor();
    cursor.setPosition( qBound( 0, cursorPosition, document()->characterCount() - 1 ) );
    setTextCursor( cursor );
}

bool SpiffsHexEditor::editHexNibble( int line, int column, QChar value ) {
    const int offset = line * 16;
    if ( offset >= m_data.size() ) return false;

    int byte = -1;
    int nibble = 0;
    for ( int i = 0; i < 16; ++i ) {
        int start = 10 + i * 3 + ( i >= 8 ? 1 : 0 );
        if ( column == start || column == start + 1 ) {
            byte = i;
            nibble = column == start ? 1 : 0;
            break;
        }
    }
    if ( byte < 0 || offset + byte >= m_data.size() ) return false;

    int digit = value.digitValue();
    if ( digit < 0 || digit > 15 ) {
        value = value.toUpper();
        if ( value < QChar( 'A' ) || value > QChar( 'F' ) ) return false;
        digit = value.unicode() - QChar( 'A' ).unicode() + 10;
    }
    uchar current = uchar( m_data.at( offset + byte ) );
    current = nibble ? uchar( ( current & 0x0f ) | ( digit << 4 ) )
                     : uchar( ( current & 0xf0 ) | digit );
    m_data[offset + byte] = char( current );
    return true;
}

bool SpiffsHexEditor::editAsciiByte( int line, int column, QChar value ) {
    const int byte = column - 61;
    const int offset = line * 16 + byte;
    if ( byte < 0 || byte >= 16 || offset >= m_data.size() || !value.isPrint() ) return false;
    m_data[offset] = char( value.toLatin1() );
    return true;
}

void SpiffsHexEditor::keyPressEvent( QKeyEvent* event ) {
    if ( !m_hexMode || isReadOnly() ) {
        QPlainTextEdit::keyPressEvent( event );
        return;
    }

    if ( event->matches( QKeySequence::Copy ) || event->matches( QKeySequence::SelectAll )
         || event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
         || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down
         || event->key() == Qt::Key_Home || event->key() == Qt::Key_End
         || event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown ) {
        QPlainTextEdit::keyPressEvent( event );
        return;
    }

    if ( event->text().isEmpty() ) return;
    QTextCursor cursor = textCursor();
    const int line = cursor.blockNumber();
    const int column = cursor.positionInBlock();
    const QChar value = event->text().at( 0 );
    bool changed = false;
    if ( column >= 10 && column < 59 )
        changed = editHexNibble( line, column, value );
    else if ( column >= 61 && column < 77 )
        changed = editAsciiByte( line, column, value );
    if ( changed ) {
        int nextColumn = column + 1;
        if ( column >= 10 && column < 59 && ( column == 11 || column == 14 || column == 17
                                               || column == 20 || column == 23 || column == 26
                                               || column == 29 || column == 32 || column == 36
                                               || column == 39 || column == 42 || column == 45
                                               || column == 48 || column == 51 || column == 54
                                               || column == 57 ) ) {
            nextColumn = column + ( column == 32 ? 3 : 2 );
        }
        updateData( cursor.position() - column + nextColumn );
    }
}

namespace {

QString normalizedPath( QString path ) {
    path = path.trimmed();
    if ( !path.startsWith( '/' ) ) path.prepend( '/' );
    while ( path.contains( "//" ) ) path.replace( "//", "/" );
    if ( path.endsWith( '/' ) ) path.chop( 1 );
    return path;
}

} // namespace

SpiffsDialog::SpiffsDialog( QWidget* parent, QemuDevice* device )
    : QDialog( parent ), m_device( device ) {
    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::Tool | Qt::WindowSystemMenuHint
                    | Qt::WindowCloseButtonHint );
    resize( 900, 600 );

    m_refreshButton = new QPushButton( QIcon( ":/reload.svg" ), tr( "Refresh" ), this );
    m_refreshButton->setToolTip( tr( "Re-read the flash image" ) );

    m_newTextButton = new QPushButton( tr( "New Text" ), this );
    m_newHexButton = new QPushButton( tr( "New Hex" ), this );
    m_importButton = new QPushButton( tr( "Import..." ), this );
    m_exportButton = new QPushButton( tr( "Export..." ), this );

    m_textButton = new QPushButton( tr( "Text" ), this );
    m_hexButton = new QPushButton( tr( "Hex" ), this );
    m_saveButton = new QPushButton( tr( "Save" ), this );
    m_deleteButton = new QPushButton( tr( "Delete" ), this );
    m_newTextButton->setEnabled( false );
    m_newHexButton->setEnabled( false );
    m_importButton->setEnabled( false );
    m_exportButton->setEnabled( false );
    m_saveButton->setEnabled( false );
    m_deleteButton->setEnabled( false );
    m_textButton->setCheckable( true );
    m_hexButton->setCheckable( true );
    m_modeGroup = new QButtonGroup( this );
    m_modeGroup->setExclusive( true );
    m_modeGroup->addButton( m_textButton );
    m_modeGroup->addButton( m_hexButton );

    m_status = new QLabel( this );
    m_status->setTextInteractionFlags( Qt::TextSelectableByMouse );

    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->addWidget( m_refreshButton );
    topLayout->addWidget( m_newTextButton );
    topLayout->addWidget( m_newHexButton );
    topLayout->addWidget( m_importButton );
    topLayout->addWidget( m_exportButton );
    topLayout->addSpacing( 8 );
    topLayout->addWidget( new QLabel( tr( "View:" ), this ) );
    topLayout->addWidget( m_textButton );
    topLayout->addWidget( m_hexButton );
    topLayout->addSpacing( 8 );
    topLayout->addWidget( m_saveButton );
    topLayout->addWidget( m_deleteButton );
    topLayout->addSpacing( 12 );
    topLayout->addWidget( m_status, 1 );

    m_fileTree = new QTreeWidget( this );
    m_fileTree->setHeaderLabels( { tr( "File" ), tr( "Size" ) } );
    m_fileTree->setRootIsDecorated( false );
    m_fileTree->setAlternatingRowColors( true );
    m_fileTree->setSelectionMode( QAbstractItemView::SingleSelection );
    m_fileTree->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_fileTree->header()->setStretchLastSection( false );
    m_fileTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
    m_fileTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );

    m_viewer = new SpiffsHexEditor( this );
    m_viewer->setReadOnly( false );
    m_viewer->setLineWrapMode( QPlainTextEdit::NoWrap );
    m_viewer->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );

    QSplitter* splitter = new QSplitter( Qt::Horizontal, this );
    splitter->addWidget( m_fileTree );
    splitter->addWidget( m_viewer );
    splitter->setStretchFactor( 0, 1 );
    splitter->setStretchFactor( 1, 3 );
    splitter->setSizes( { 260, 640 } );

    QVBoxLayout* layout = new QVBoxLayout( this );
    layout->addLayout( topLayout );
    layout->addWidget( splitter, 1 );

    connect( m_refreshButton, &QPushButton::clicked, this, &SpiffsDialog::refresh );
    connect( m_newTextButton, &QPushButton::clicked, this, [this]() { createFile( true ); } );
    connect( m_newHexButton, &QPushButton::clicked, this, [this]() { createFile( false ); } );
    connect( m_importButton, &QPushButton::clicked, this, &SpiffsDialog::importFile );
    connect( m_exportButton, &QPushButton::clicked, this, &SpiffsDialog::exportFile );
    connect( m_textButton, &QPushButton::clicked, this, [this]() { setTextMode( true ); } );
    connect( m_hexButton, &QPushButton::clicked, this, [this]() { setTextMode( false ); } );
    connect( m_saveButton, &QPushButton::clicked, this, [this]() { saveCurrent(); } );
    connect( m_deleteButton, &QPushButton::clicked, this, [this]() { removeCurrent(); } );
    connect( m_fileTree, &QTreeWidget::currentItemChanged, this,
             [this]( QTreeWidgetItem* current, QTreeWidgetItem* ) { selectItem( current ); } );
}

void SpiffsDialog::refresh() {
    QString path = writablePath();
    if ( path.isEmpty() )
        path = m_device->firmware();

    m_entries.clear();
    m_currentData.clear();
    m_currentPath.clear();
    m_fileTree->clear();
    m_viewer->clear();

    if ( path.isEmpty() ) {
        m_status->setText( tr( "No flash image" ) );
        m_saveButton->setEnabled( false );
        m_deleteButton->setEnabled( false );
        m_newTextButton->setEnabled( false );
        m_newHexButton->setEnabled( false );
        m_importButton->setEnabled( false );
        m_exportButton->setEnabled( false );
        return;
    }

    updateStats( path );

    QString error;
    m_entries = SpiffsImage::read( path, &error );

    if ( m_entries.isEmpty() ) {
        QString message = error.isEmpty() ? tr( "No SPIFFS files found" ) : error;
        if ( !m_status->text().isEmpty() ) message += " — " + m_status->text();
        m_status->setText( message );
        m_saveButton->setEnabled( false );
        m_deleteButton->setEnabled( false );
        m_exportButton->setEnabled( false );
        return;
    }

    for ( int i = 0; i < m_entries.size(); ++i ) {
        const SpiffsEntry& entry = m_entries.at( i );
        QTreeWidgetItem* item = new QTreeWidgetItem( m_fileTree );
        item->setText( 0, entry.path );
        item->setText( 1, sizeText( entry.size ) );
        item->setData( 0, Qt::UserRole, i );
    }

    QString fileStatus = tr( "%1 files in %2" ).arg( m_entries.size() ).arg( QFileInfo( path ).fileName() );
    if ( !m_status->text().isEmpty() ) fileStatus += " — " + m_status->text();
    m_status->setText( fileStatus );
    m_fileTree->setCurrentItem( m_fileTree->topLevelItem( 0 ) );
}

QString SpiffsDialog::writablePath() const {
    return m_device->flashPath();
}

void SpiffsDialog::updateStats( const QString& path ) {
    QString error;
    SpiffsStats info = SpiffsImage::stats( path, &error );
    if ( info.totalBytes == 0 ) {
        m_status->setText( error );
        return;
    }
    QString mode = info.hasPartition ? QString() : tr( " (read-only image)" );
    m_status->setText( tr( "Used %1 / Free %2 / Total %3%4" )
                           .arg( sizeText( info.usedBytes ) )
                           .arg( sizeText( info.freeBytes ) )
                           .arg( sizeText( info.totalBytes ) )
                           .arg( mode ) );
    bool writable = !writablePath().isEmpty();
    m_newTextButton->setEnabled( writable );
    m_newHexButton->setEnabled( writable );
    m_importButton->setEnabled( writable );
}

void SpiffsDialog::selectItem( QTreeWidgetItem* item ) {
    if ( !item ) {
        m_currentData.clear();
        m_currentPath.clear();
        m_viewer->clear();
        m_saveButton->setEnabled( false );
        m_deleteButton->setEnabled( false );
        m_exportButton->setEnabled( false );
        return;
    }

    int index = item->data( 0, Qt::UserRole ).toInt();
    if ( index < 0 || index >= m_entries.size() )
        return;

    const SpiffsEntry& entry = m_entries.at( index );
    m_currentData = entry.data;
    m_currentPath = entry.path;

    bool text = isTextPath( entry.path );
    {
        QSignalBlocker textBlocker( m_textButton );
        QSignalBlocker hexBlocker( m_hexButton );
        m_textButton->setChecked( text );
        m_hexButton->setChecked( !text );
    }
    m_textMode = text;
    m_viewer->setHexMode( !text );
    bool writable = !writablePath().isEmpty();
    m_viewer->setReadOnly( !writable );
    m_saveButton->setEnabled( writable );
    m_deleteButton->setEnabled( writable );
    m_exportButton->setEnabled( true );
    renderCurrent();
}

void SpiffsDialog::setTextMode( bool text ) {
    if ( m_textMode == text )
        return;
    if ( !m_truncated )
        m_currentData = m_textMode ? m_viewer->toPlainText().toUtf8() : m_viewer->hexData();
    m_textMode = text;
    m_viewer->setHexMode( !text );
    QSignalBlocker textBlocker( m_textButton );
    QSignalBlocker hexBlocker( m_hexButton );
    m_textButton->setChecked( text );
    m_hexButton->setChecked( !text );
    m_viewer->setReadOnly( writablePath().isEmpty() );
    m_saveButton->setEnabled( !m_currentPath.isEmpty() && !writablePath().isEmpty() );
    renderCurrent();
}

void SpiffsDialog::saveCurrent() {
    if ( m_currentPath.isEmpty() ) return;
    const QString savedPath = m_currentPath;
    const bool savedTextMode = m_textMode;
    QString path = writablePath();
    if ( path.isEmpty() || m_truncated ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), tr( "The flash image is not writable" ) );
        return;
    }
    QByteArray data;
    QString error;
    if ( m_textMode ) data = m_viewer->toPlainText().toUtf8();
    else data = m_viewer->hexData();
    if ( !SpiffsImage::writeFile( path, m_currentPath, data, &error ) ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), error );
        return;
    }
    refresh();
    for ( int i = 0; i < m_fileTree->topLevelItemCount(); ++i ) {
        QTreeWidgetItem* item = m_fileTree->topLevelItem( i );
        if ( item->text( 0 ) == savedPath ) {
            m_fileTree->setCurrentItem( item );
            break;
        }
    }
    if ( m_textMode != savedTextMode ) setTextMode( savedTextMode );
}

void SpiffsDialog::createFile( bool text ) {
    QString path = writablePath();
    if ( path.isEmpty() ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), tr( "The flash image is not writable" ) );
        return;
    }
    bool ok = false;
    QString filePath = QInputDialog::getText( this, tr( "Create SPIFFS file" ), tr( "Path:" ),
                                              QLineEdit::Normal, "/new-file", &ok );
    filePath = normalizedPath( filePath );
    if ( !ok || filePath == "/" ) return;
    for ( const SpiffsEntry& entry : m_entries ) {
        if ( entry.path == filePath ) {
            QMessageBox::warning( this, tr( "SPIFFS" ), tr( "File already exists: %1" ).arg( filePath ) );
            return;
        }
    }
    QString error;
    if ( !SpiffsImage::writeFile( path, filePath, QByteArray(), &error ) ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), error );
        return;
    }
    refresh();
    for ( int i = 0; i < m_fileTree->topLevelItemCount(); ++i ) {
        QTreeWidgetItem* item = m_fileTree->topLevelItem( i );
        if ( item->text( 0 ) == filePath ) {
            m_fileTree->setCurrentItem( item );
            break;
        }
    }
    if ( m_textMode != text ) setTextMode( text );
}

void SpiffsDialog::importFile() {
    QString path = writablePath();
    if ( path.isEmpty() ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), tr( "The flash image is not writable" ) );
        return;
    }
    QString sourcePath = QFileDialog::getOpenFileName( this, tr( "Import file to SPIFFS" ) );
    if ( sourcePath.isEmpty() ) return;
    QFile source( sourcePath );
    if ( !source.open( QIODevice::ReadOnly ) ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), tr( "Cannot read file: %1" ).arg( sourcePath ) );
        return;
    }
    QByteArray data = source.readAll();
    bool ok = false;
    QString filePath = QInputDialog::getText( this, tr( "Import file to SPIFFS" ), tr( "SPIFFS path:" ),
                                              QLineEdit::Normal, "/" + QFileInfo( sourcePath ).fileName(), &ok );
    filePath = normalizedPath( filePath );
    if ( !ok || filePath == "/" ) return;
    QString error;
    if ( !SpiffsImage::writeFile( path, filePath, data, &error ) ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), error );
        return;
    }
    refresh();
}

void SpiffsDialog::exportFile() {
    if ( m_currentPath.isEmpty() ) return;
    QString targetPath = QFileDialog::getSaveFileName( this, tr( "Export SPIFFS file" ),
                                                       QFileInfo( m_currentPath ).fileName() );
    if ( targetPath.isEmpty() ) return;
    QFile target( targetPath );
    if ( !target.open( QIODevice::WriteOnly ) || target.write( m_currentData ) != m_currentData.size() ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), tr( "Cannot write file: %1" ).arg( targetPath ) );
        return;
    }
    m_status->setText( tr( "Exported %1" ).arg( targetPath ) );
}

void SpiffsDialog::removeCurrent() {
    if ( m_currentPath.isEmpty() ) return;
    if ( QMessageBox::question( this, tr( "Delete file" ),
                                tr( "Delete %1?" ).arg( m_currentPath ),
                                QMessageBox::Yes | QMessageBox::No ) != QMessageBox::Yes ) return;
    QString path = writablePath();
    QString error;
    if ( path.isEmpty() || !SpiffsImage::removeFile( path, m_currentPath, &error ) ) {
        QMessageBox::warning( this, tr( "SPIFFS" ), error );
        return;
    }
    refresh();
}

void SpiffsDialog::renderCurrent() {
    if ( m_currentPath.isEmpty() ) {
        m_viewer->clear();
        return;
    }

    m_truncated = false;
    if ( m_textMode ) {
        const int limit = 4 * 1024 * 1024;
        QByteArray data = m_currentData;
        if ( data.size() > limit ) {
            data.truncate( limit );
            m_truncated = true;
        }
        QString text = QString::fromUtf8( data.constData(), data.size() );
        if ( m_truncated )
            text += tr( "\n\n... truncated ..." );
        m_viewer->setPlainText( text );
    } else {
        const int limit = 1024 * 1024;
        QByteArray data = m_currentData;
        if ( data.size() > limit ) {
            data.truncate( limit );
            m_truncated = true;
        }
        m_viewer->setHexData( data );
        if ( m_truncated ) m_viewer->appendPlainText( tr( "... truncated ..." ) );
    }
    m_saveButton->setEnabled( !m_currentPath.isEmpty() && !m_truncated && !writablePath().isEmpty() );
    m_viewer->moveCursor( QTextCursor::Start );
}
