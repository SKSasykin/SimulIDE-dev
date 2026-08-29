/***************************************************************************
 *   Copyright (C) 2024 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QDir>

class Component;

class BatchTest {
public:
    static void doBatchTest( QString folder, int timeoutMs = 30000 );

    static bool isRunning() { return m_running; }
    static void addTestUnit( Component* c );
    static void testCompleted( Component* c, bool ok );

    static void checkFinished();

private:
    static void prepareTest( QDir dir );
    static void runNextCircuit();
    static void failCurrentTest( QString reason, int testId );
    static void finishBatch( int exitCode );

    static bool m_running;
    static int m_testId;
    static int m_timeoutMs;

    static QString m_currentFile;

    static QStringList m_failedTests;
    static QStringList m_circFiles;
    static QList<Component*> m_testUnits;
};
