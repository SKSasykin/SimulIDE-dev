/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QList>
#include <vector>

#include "e-node.h"

class CircMatrix {
    typedef std::vector<double> d_vector_t;
    typedef std::vector<double*> dp_vector_t;
    typedef std::vector<d_vector_t> d_matrix_t;
    typedef std::vector<dp_vector_t> dp_matrix_t;

public:
    CircMatrix();
    ~CircMatrix();

    static CircMatrix* self() { return m_pSelf; }

    void createMatrix( QList<eNode*>& eNodeList );
    bool solveMatrix();

    inline void stampDiagonal( int group, int n, double value ) {
        m_admitChanged[group] = true;
        m_circMatrix[n][n] = value; // eNode numbers start at 1
    }
    inline void stampMatrix( int row, int col, double value ) {
        m_circMatrix[row][col] = value; // eNode numbers start at 1
    }
    inline void stampCoef( int group, int row, double value ) {
        m_currChanged[group] = true;
        m_coefVect[row] = value;
    }

private:
    static CircMatrix* m_pSelf;

    void analyze();
    void addConnections( int enodNum, QList<int>* nodeGroup, QList<int>* allNodes );

    inline void factorMatrix( int n, int group );
    inline bool luSolve( int n, int group );

    int m_numEnodes;
    QList<eNode*>* m_eNodeList;

    QList<dp_matrix_t> m_aList;
    QList<d_matrix_t> m_aFaList;
    QList<dp_vector_t> m_bList;

    std::vector<bool> m_admitChanged;
    std::vector<bool> m_currChanged;
    QList<eNode*>* m_eNodeActive;
    QList<QList<eNode*>> m_eNodeActList;

    d_matrix_t m_circMatrix;
    d_vector_t m_coefVect;
};
