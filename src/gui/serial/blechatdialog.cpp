#include "blechatdialog.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QVBoxLayout>

#include "blechatclient.h"
#include "blechatformat.h"
#include "simulator.h"

BleChatDialog::BleChatDialog( QWidget* parent )
    : QDialog( parent )
    , m_client( new BleChatClient( this ) )
{
    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::Tool | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint );
    setMinimumSize( 520, 420 );

    QVBoxLayout* mainLayout = new QVBoxLayout( this );

    QHBoxLayout* topRow = new QHBoxLayout();
    m_scanButton = new QPushButton( tr( "Scan" ), this );
    m_scanButton->setToolTip( tr( "Scan for advertising peripherals" ) );
    m_connectButton = new QPushButton( tr( "Connect" ), this );
    m_connectButton->setToolTip( tr( "Connect to the selected peripheral" ) );
    m_disconnectButton = new QPushButton( tr( "Disconnect" ), this );
    m_disconnectButton->setToolTip( tr( "Disconnect from the peripheral" ) );
    m_discoverButton = new QPushButton( tr( "Discover" ), this );
    m_discoverButton->setToolTip( tr( "Discover services and characteristics again" ) );
    topRow->addWidget( m_scanButton );
    topRow->addWidget( m_connectButton );
    topRow->addWidget( m_disconnectButton );
    topRow->addWidget( m_discoverButton );
    mainLayout->addLayout( topRow );

    m_deviceList = new QListWidget( this );
    m_deviceList->setMaximumHeight( 90 );
    mainLayout->addWidget( m_deviceList );

    m_statusLabel = new QLabel( tr( "Disconnected" ), this );
    m_attrLabel = new QLabel( tr( "No characteristic" ), this );
    mainLayout->addWidget( m_statusLabel );
    mainLayout->addWidget( m_attrLabel );

    m_log = new QTextEdit( this );
    m_log->setReadOnly( true );
    m_log->setFontFamily( "Courier New" );
    mainLayout->addWidget( m_log, 1 );

    QHBoxLayout* bottomRow = new QHBoxLayout();
    m_formatBox = new QComboBox( this );
    m_formatBox->addItem( "String" );
    m_formatBox->addItem( "HEX" );
    m_input = new QLineEdit( this );
    m_input->setPlaceholderText( tr( "Type message, use \\xNN in String mode" ) );
    m_input->setToolTip( tr( "Value to write to the characteristic" ) );
    m_sendButton = new QPushButton( tr( "Send" ), this );
    m_sendButton->setToolTip( tr( "Write the input value to the characteristic" ) );
    bottomRow->addWidget( m_formatBox );
    bottomRow->addWidget( m_input, 1 );
    bottomRow->addWidget( m_sendButton );
    bottomRow->addWidget( m_clearButton );
    mainLayout->addLayout( bottomRow );

    connect( m_scanButton, &QPushButton::clicked, this, &BleChatDialog::onScanClicked );
    connect( m_connectButton, &QPushButton::clicked, this, &BleChatDialog::onConnectClicked );
    connect( m_disconnectButton, &QPushButton::clicked, this, &BleChatDialog::onDisconnectClicked );
    connect( m_discoverButton, &QPushButton::clicked, this, &BleChatDialog::onDiscoverClicked );
    connect( m_sendButton, &QPushButton::clicked, this, &BleChatDialog::onSendClicked );
    connect( m_clearButton, &QPushButton::clicked, this, &BleChatDialog::onClearClicked );
    connect( m_input, &QLineEdit::returnPressed, this, &BleChatDialog::onSendClicked );

    connect( m_client, &BleChatClient::devicesChanged, this, &BleChatDialog::onDevicesChanged );
    connect( m_client, &BleChatClient::connectionChanged, this, &BleChatDialog::onConnectionChanged );
    connect( m_client, &BleChatClient::discoveryChanged, this, &BleChatDialog::onDiscoveryChanged );
    connect( m_client, &BleChatClient::valueRead, this, &BleChatDialog::onValueRead );
    connect( m_client, &BleChatClient::writeDone, this, &BleChatDialog::onWriteDone );
    connect( m_client, &BleChatClient::notification, this, &BleChatDialog::onNotification );
    connect( m_client, &BleChatClient::statusMessage, this, &BleChatDialog::onStatusMessage );
    connect( m_client, &BleChatClient::errorMessage, this, &BleChatDialog::onErrorMessage );

    if ( Simulator::self() ) Simulator::self()->addToUpdateList( this );
    onConnectionChanged();
}

BleChatDialog::~BleChatDialog()
{
}

void BleChatDialog::showEvent( QShowEvent* event )
{
    QDialog::showEvent( event );
    if ( !m_clientStarted ) {
        m_client->start();
        m_clientStarted = true;
    }
}

void BleChatDialog::closeEvent( QCloseEvent* event )
{
    event->accept();
}

void BleChatDialog::updateStep()
{
    if ( !m_clientStarted ) return;
    m_client->poll();
    if ( Simulator::self() && !Simulator::self()->isRunning() && m_client->scanning() )
        m_client->stopScan();
}

bool BleChatDialog::hexMode() const
{
    return m_formatBox->currentText() == "HEX";
}

QString BleChatDialog::formatData( const QByteArray& data ) const
{
    if ( hexMode() ) return bleChatFormatHex( data );
    return bleChatFormatString( data );
}

bool BleChatDialog::parseInput( QByteArray& out, QString& error ) const
{
    QString text = m_input->text();
    if ( hexMode() ) return bleChatParseHex( text, out, error );
    return bleChatParseString( text, out, error );
}

void BleChatDialog::appendLog( const QString& prefix, const QString& text, const QString& color )
{
    m_log->append( "<font color='" + color + "'>" + prefix + "</font> " + text.toHtmlEscaped() );
}

void BleChatDialog::appendInfo( const QString& text )
{
    m_log->append( "<i>" + text.toHtmlEscaped() + "</i>" );
}

void BleChatDialog::onScanClicked()
{
    if ( !m_clientStarted ) {
        m_client->start();
        m_clientStarted = true;
    }
    if ( m_client->scanning() ) m_client->stopScan();
    else m_client->startScan();
    onConnectionChanged();
}

void BleChatDialog::onConnectClicked()
{
    int row = m_deviceList->currentRow();
    if ( row < 0 && m_deviceList->count() > 0 ) row = 0;
    if ( row < 0 ) {
        appendInfo( tr( "No device to connect" ) );
        return;
    }
    m_client->connectToDevice( row );
}

void BleChatDialog::onDisconnectClicked()
{
    m_client->disconnect();
}

void BleChatDialog::onDiscoverClicked()
{
    m_client->discover();
}

void BleChatDialog::onSendClicked()
{
    QByteArray data;
    QString error;
    if ( !parseInput( data, error ) ) {
        appendInfo( tr( "Bad input: " ) + error );
        return;
    }
    if ( data.isEmpty() ) return;
    appendLog( "TX", formatData( data ), "yellow" );
    m_client->writeValue( data );
}

void BleChatDialog::onClearClicked()
{
    m_log->clear();
}

void BleChatDialog::onDevicesChanged()
{
    int current = m_deviceList->currentRow();
    m_deviceList->clear();
    QList<BleChatDevice> devices = m_client->devices();
    for ( int i = 0; i < devices.size(); ++i ) {
        const BleChatDevice& device = devices.at( i );
        QString label = device.addressText;
        if ( !device.name.isEmpty() ) label += " \"" + device.name + "\"";
        label += " rssi=" + QString::number( device.rssi );
        m_deviceList->addItem( label );
    }
    if ( current >= 0 && current < m_deviceList->count() ) m_deviceList->setCurrentRow( current );
    else if ( m_deviceList->count() > 0 ) m_deviceList->setCurrentRow( 0 );
}

void BleChatDialog::onConnectionChanged()
{
    m_statusLabel->setText( m_client->connectionText() );
    m_scanButton->setText( m_client->scanning() ? tr( "Stop" ) : tr( "Scan" ) );
    bool linked = m_client->connected();
    bool busy = m_client->connecting();
    m_connectButton->setEnabled( !linked && !busy );
    m_disconnectButton->setEnabled( linked || busy );
    m_discoverButton->setEnabled( linked );
    m_sendButton->setEnabled( m_client->ready() );
}

void BleChatDialog::onDiscoveryChanged()
{
    m_attrLabel->setText( m_client->attributeText() );
    onConnectionChanged();
}

void BleChatDialog::onValueRead( QByteArray data )
{
    appendLog( "RX read", formatData( data ), "lightgreen" );
}

void BleChatDialog::onWriteDone( bool ok, QString info )
{
    if ( ok ) appendInfo( tr( "Write OK" ) );
    else appendInfo( tr( "Write failed: " ) + info );
}

void BleChatDialog::onNotification( QByteArray data )
{
    appendLog( "RX notify", formatData( data ), "lightgreen" );
}

void BleChatDialog::onStatusMessage( QString text )
{
    appendInfo( text );
    onConnectionChanged();
}

void BleChatDialog::onErrorMessage( QString text )
{
    appendInfo( tr( "Error: " ) + text );
    onConnectionChanged();
}
