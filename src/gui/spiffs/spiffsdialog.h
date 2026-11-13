#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QVector>

#include "spiffsimage.h"

class QButtonGroup;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QemuDevice;
class QKeyEvent;

class SpiffsHexEditor : public QPlainTextEdit {
public:
    explicit SpiffsHexEditor( QWidget* parent = nullptr );

    void setHexData( const QByteArray& data );
    QByteArray hexData() const { return m_data; }
    void setHexMode( bool enabled ) { m_hexMode = enabled; }

protected:
    void keyPressEvent( QKeyEvent* event ) override;

private:
    bool editHexNibble( int line, int column, QChar value );
    bool editAsciiByte( int line, int column, QChar value );
    void updateData( int cursorPosition );

    QByteArray m_data;
    bool m_hexMode = false;
};

class SpiffsDialog : public QDialog {
    Q_OBJECT

public:
    SpiffsDialog( QWidget* parent, QemuDevice* device );

    void refresh();

private:
    void selectItem( QTreeWidgetItem* item );
    void renderCurrent();
    void setTextMode( bool text );
    void saveCurrent();
    void removeCurrent();
    void createFile( bool text );
    void importFile();
    void exportFile();
    void updateStats( const QString& path );
    QString writablePath() const;

    QemuDevice* m_device;
    QTreeWidget* m_fileTree;
    SpiffsHexEditor* m_viewer;
    QLabel* m_status;
    QPushButton* m_refreshButton;
    QPushButton* m_newTextButton;
    QPushButton* m_newHexButton;
    QPushButton* m_importButton;
    QPushButton* m_exportButton;
    QPushButton* m_textButton;
    QPushButton* m_hexButton;
    QPushButton* m_saveButton;
    QPushButton* m_deleteButton;
    QButtonGroup* m_modeGroup;

    QVector<SpiffsEntry> m_entries;
    QByteArray m_currentData;
    QString m_currentPath;
    bool m_textMode = true;
    bool m_truncated = false;
};
