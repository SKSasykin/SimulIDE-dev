#pragma once

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>

#include "updatable.h"

class BleChatClient;

class BleChatDialog : public QDialog, public Updatable {
    Q_OBJECT

public:
    explicit BleChatDialog( QWidget* parent = nullptr );
    ~BleChatDialog();

    void updateStep() override;

protected:
    void closeEvent( QCloseEvent* event ) override;
    void showEvent( QShowEvent* event ) override;

private slots:
    void onScanClicked();
    void onConnectClicked();
    void onDisconnectClicked();
    void onDiscoverClicked();
    void onServiceChanged( int index );
    void onCharChanged( int index );
    void onSendClicked();
    void onClearClicked();
    void onDevicesChanged();
    void onConnectionChanged();
    void onDiscoveryChanged();
    void onValueRead( QByteArray data );
    void onWriteDone( bool ok, QString info );
    void onNotification( QByteArray data );
    void onStatusMessage( QString text );
    void onErrorMessage( QString text );

private:
    void appendLog( const QString& prefix, const QString& text, const QString& color );
    void appendInfo( const QString& text );
    QString formatData( const QByteArray& data ) const;
    bool parseInput( QByteArray& out, QString& error ) const;
    bool hexMode() const;
    void refreshAttributeBoxes();
    void refreshCharBox( int svc );

    BleChatClient* m_client;
    bool m_clientStarted = false;
    bool m_lastSimOn = false;

    QListWidget* m_deviceList;
    QLabel* m_statusLabel;
    QLabel* m_attrLabel;
    QComboBox* m_serviceBox;
    QComboBox* m_charBox;
    QTextEdit* m_log;
    QComboBox* m_formatBox;
    QLineEdit* m_input;
    QPushButton* m_scanButton;
    QPushButton* m_connectButton;
    QPushButton* m_disconnectButton;
    QPushButton* m_discoverButton;
    QPushButton* m_sendButton;
    QPushButton* m_clearButton;
};
