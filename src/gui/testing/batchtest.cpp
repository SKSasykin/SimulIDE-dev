/***************************************************************************
 *   Copyright (C) 2024 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QDebug>
#include <QCoreApplication>
#include <QTimer>

#include "batchtest.h"
#include "circuitwidget.h"
#include "component.h"

bool BatchTest::m_running = false;
int BatchTest::m_testId = 0;
int BatchTest::m_timeoutMs = 30000;
QString BatchTest::m_currentFile;
QStringList BatchTest::m_failedTests;
QStringList BatchTest::m_circFiles;
QList<Component*> BatchTest::m_testUnits;

void BatchTest::doBatchTest( QString folder, int timeoutMs ) {
    QDir dir = QDir( folder );
    if ( !dir.exists() ) {
        qCritical() << "TEST ERROR: folder doesn't exist:" << folder;
        finishBatch( 2 );
        return;
    }
    m_timeoutMs = timeoutMs;
    m_failedTests.clear();
    m_circFiles.clear();

    prepareTest( dir );

    runNextCircuit();
}

void BatchTest::prepareTest( QDir baseDir ) {
    QStringList circList = baseDir.entryList( { "*.sim2", "*.sim1" }, QDir::Files );

    for ( QString file : circList )
        m_circFiles.append( baseDir.absoluteFilePath( file ) );

    QStringList dirList = baseDir.entryList( { "*" }, QDir::Dirs );
    for ( QString dir : dirList ) {
        if ( dir == "." || dir == ".." )
            continue;
        if ( !baseDir.cd( dir ) )
            continue;

        prepareTest( baseDir );
        baseDir.cd( ".." );
    }
}

void BatchTest::runNextCircuit() {
    CircuitWidget::self()->powerCircOff();

    if ( m_circFiles.isEmpty() ) // All tests completed
    {
        m_running = false;

        if ( m_failedTests.isEmpty() ) {
            qDebug() << "TEST PASS: all circuit tests passed";
            finishBatch( 0 );
        }
        else {
            qCritical() << "TEST FAIL:" << m_failedTests.size() << "circuit test(s) failed:";
            for ( QString file : m_failedTests )
                qCritical() << " -" << file;
            finishBatch( 1 );
        }
        return;
    }
    m_currentFile = m_circFiles.takeFirst();
    const int testId = ++m_testId;

    qDebug() << "TEST RUN:" << m_currentFile;
    m_testUnits.clear();
    m_running = true;
    CircuitWidget::self()->loadCirc( m_currentFile );

    CircuitWidget::self()->powerCircOn();
    QTimer::singleShot( 100, [testId]() {
        if ( testId == m_testId && m_running && m_testUnits.isEmpty() )
            failCurrentTest( "no active test components", testId );
    } );
    QTimer::singleShot( m_timeoutMs, [testId]() {
        if ( testId == m_testId && m_running )
            failCurrentTest( "timeout", testId );
    } );
    checkFinished();
}

void BatchTest::checkFinished() {
    if ( m_running )
        QTimer::singleShot( 100, BatchTest::checkFinished );
    else
        BatchTest::runNextCircuit();
}

void BatchTest::addTestUnit( Component* c ) {
    if ( !m_testUnits.contains( c ) )
        m_testUnits.append( c );
}

void BatchTest::testCompleted( Component* c, bool ok ) // A test unit completed (could be more in this Circuit)
{
    m_testUnits.removeAll( c );

    if ( !ok ) { // Test failed
        if ( !m_failedTests.contains( m_currentFile ) )
            m_failedTests.append( m_currentFile );
    }
    if ( m_testUnits.isEmpty() )
        m_running = false; // All test units in this Circuit finished
}

void BatchTest::failCurrentTest( QString reason, int testId ) {
    if ( testId != m_testId || !m_running )
        return;
    qCritical() << "TEST FAIL:" << m_currentFile << "(" + reason + ")";
    if ( !m_failedTests.contains( m_currentFile ) )
        m_failedTests.append( m_currentFile );
    m_running = false;
}

void BatchTest::finishBatch( int exitCode ) {
    QTimer::singleShot( 0, [exitCode]() { QCoreApplication::exit( exitCode ); } );
}
