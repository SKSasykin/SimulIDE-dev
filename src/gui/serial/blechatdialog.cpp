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

    QHBoxLayout* serviceRow = new QHBoxLayout();
    QLabel* serviceLabel = new QLabel( tr( "Service:" ), this );
    m_serviceBox = new QComboBox( this );
    m_serviceBox->setToolTip( tr( "GATT service to chat with; changing it resubscribes" ) );
    m_serviceBox->setEnabled( false );
    serviceRow->addWidget( serviceLabel );
    serviceRow->addWidget( m_serviceBox, 1 );
    mainLayout->addLayout( serviceRow );

    QHBoxLayout* charRow = new QHBoxLayout();
    QLabel* charLabel = new QLabel( tr( "Characteristic:" ), this );
    m_charBox = new QComboBox( this );
    m_charBox->setToolTip( tr( "GATT characteristic to read, write and notify" ) );
    m_charBox->setEnabled( false );
    charRow->addWidget( charLabel );
    charRow->addWidget( m_charBox, 1 );
    mainLayout->addLayout( charRow );

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
    m_clearButton = new QPushButton( tr( "Clear" ), this );
    m_clearButton->setToolTip( tr( "Clear the chat log" ) );
    bottomRow->addWidget( m_formatBox );
    bottomRow->addWidget( m_input, 1 );
    bottomRow->addWidget( m_sendButton );
    bottomRow->addWidget( m_clearButton );
    mainLayout->addLayout( bottomRow );

    connect( m_scanButton, &QPushButton::clicked, this, &BleChatDialog::onScanClicked );
    connect( m_connectButton, &QPushButton::clicked, this, &BleChatDialog::onConnectClicked );
    connect( m_disconnectButton, &QPushButton::clicked, this, &BleChatDialog::onDisconnectClicked );
    connect( m_discoverButton, &QPushButton::clicked, this, &BleChatDialog::onDiscoverClicked );
    connect( m_serviceBox, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &BleChatDialog::onServiceChanged );
    connect( m_charBox, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, &BleChatDialog::onCharChanged );
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
    const bool simOn = Simulator::self() && Simulator::self()->isRunning();
    if ( simOn != m_lastSimOn ) {
        m_lastSimOn = simOn;
        onConnectionChanged();
    }
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
    onConnectionChanged();
}

void BleChatDialog::onConnectionChanged()
{
    m_statusLabel->setText( m_client->connectionText() );
    m_scanButton->setText( m_client->scanning() ? tr( "Stop" ) : tr( "Scan" ) );
    const bool simOn = Simulator::self() && Simulator::self()->isRunning();
    bool linked = m_client->connected();
    bool busy = m_client->connecting();
    m_scanButton->setEnabled( simOn );
    m_connectButton->setEnabled( simOn && !linked && !busy && m_deviceList->count() > 0 );
    m_disconnectButton->setEnabled( linked || busy );
    m_discoverButton->setEnabled( simOn && linked );
    m_sendButton->setEnabled( simOn && m_client->ready() && m_client->targetWritable() );
}

void BleChatDialog::onDiscoveryChanged()
{
    m_attrLabel->setText( m_client->attributeText() );
    refreshAttributeBoxes();
    onConnectionChanged();
}

void BleChatDialog::refreshAttributeBoxes()
{
    const bool ready = m_client->ready();
    m_serviceBox->blockSignals( true );
    m_charBox->blockSignals( true );
    m_serviceBox->clear();
    m_charBox->clear();
    m_serviceBox->setEnabled( ready && m_client->serviceCount() > 0 );
    m_charBox->setEnabled( false );
    if ( ready ) {
        for ( int i = 0; i < m_client->serviceCount(); ++i )
            m_serviceBox->addItem( m_client->serviceText( i ) );
        const int svc = m_client->selectedService();
        if ( svc >= 0 && svc < m_serviceBox->count() ) {
            m_serviceBox->setCurrentIndex( svc );
            refreshCharBox( svc );
        }
    }
    m_serviceBox->blockSignals( false );
    m_charBox->blockSignals( false );
}

void BleChatDialog::refreshCharBox( int svc )
{
    m_charBox->clear();
    m_charBox->setEnabled( m_client->ready() && m_client->charCount( svc ) > 0 );
    for ( int i = 0; i < m_client->charCount( svc ); ++i )
        m_charBox->addItem( m_client->charText( svc, i ) );
    const int pos = m_client->selectedChar();
    if ( svc == m_client->selectedService() && pos >= 0 && pos < m_charBox->count() )
        m_charBox->setCurrentIndex( pos );
    else if ( m_charBox->count() > 0 )
        m_charBox->setCurrentIndex( 0 );
}

void BleChatDialog::onServiceChanged( int index )
{
    if ( !m_client->ready() || index < 0 ) return;
    m_charBox->blockSignals( true );
    refreshCharBox( index );
    m_charBox->blockSignals( false );
    m_client->selectTarget( index, -1 );
}

void BleChatDialog::onCharChanged( int index )
{
    if ( !m_client->ready() || index < 0 ) return;
    m_client->selectTarget( m_serviceBox->currentIndex(), index );
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
