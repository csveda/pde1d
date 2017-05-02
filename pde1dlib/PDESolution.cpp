/*
 * PDESolution.cpp
 *
 *  Created on: Apr 30, 2017
 *      Author: bgreene
 */
#include <iostream>
#include <fstream>

using std::cout;
using std::endl;

#include "PDESolution.h"
#include "PDE1dDefn.h"
#include "PDEModel.h"

PDESolution::PDESolution(const PDE1dDefn &pde, const PDEModel &model,
  int numViewElemsPerElem) :
  initialX(pde.getMesh()),
  time(pde.getTimeSpan()), numViewElemsPerElem(numViewElemsPerElem),
  pde(pde), model(model)
{
  size_t numTimes = time.size();
  int numPDE = pde.getNumPDE();
  if (numViewElemsPerElem == 1) {
    x = initialX;
    size_t nnOrig = x.size();
    u.resize(numTimes, nnOrig*numPDE);
    const size_t nnfee = model.numNodesFEEqns();
    u2Tmp.resize(1, nnOrig*numPDE);
  }
  else {
    size_t ne = initialX.size() - 1;
    numNodesViewMesh = numViewElemsPerElem*ne + 1;
    x.resize(numNodesViewMesh);
    u.resize(numTimes, numNodesViewMesh*numPDE);
    u2Tmp.resize(numPDE, numNodesViewMesh);
    x(0) = initialX(0);
    double xij = x(0);
    size_t ij = 1;
    for (int i = 0; i < ne; i++) {
      double L = initialX(i + 1) - initialX(i);
      double dx = L / (double)numViewElemsPerElem;
      for (int j = 0; j < numViewElemsPerElem; j++) {
        xij += dx;
        x(ij++) = xij;
      }
    }
  }
}

void PDESolution::setSolutionVector(int timeStep, double time,
  const RealVector &uSol)
{
  int numDepVars = pde.getNumPDE();
  const size_t nnfee = model.numNodesFEEqns();
  if (numViewElemsPerElem == 1) {
    model.globalToMeshVec(uSol.topRows(nnfee), u2Tmp);
    u.row(timeStep) = u2Tmp;
#if 0
    int numDepVars = pde.getNumPDE();
    const size_t nnfee = model.numNodesFEEqns();
    typedef Eigen::Map<const Eigen::MatrixXd> ConstMapMat;
    ConstMapMat u2Sol(uSol.data(), numDepVars, nnfee);
    cout << "u2Sol=" << u2Sol << endl;
#endif
  }
  else {
    typedef Eigen::Map<const Eigen::MatrixXd> ConstMapMat;
    ConstMapMat u2Sol(uSol.data(), numDepVars, nnfee);
    PDEModel::DofList eDofs;
    RealMatrix u2e(numDepVars, 2);
    size_t numR = numViewElemsPerElem - 1;
    double dr = 2. / (double)numViewElemsPerElem;
    RealVector rVals(numR);
    double r = -1;
    for (int i = 0; i < numR; i++) {
      r += dr;
      rVals(i) = r;
    }
    //cout << "rvals=" << rVals << endl;
    RealMatrix N, u2eView;
    size_t ne = model.numElements();
    int lastNumElemNodes = 0;
    size_t iViewNode = 0;
    for (int e = 0; e < ne; e++) {
      const PDEElement &elem = model.element(e);
      model.getDofIndicesForElem(e, eDofs);
      PDEModel::globalToElemVec(eDofs, u2Sol, u2e);
      const ShapeFunction &sf = elem.getSF().getShapeFunction();
      if (sf.numNodes() != lastNumElemNodes) {
        lastNumElemNodes = sf.numNodes();
        N.resize(lastNumElemNodes, numR);
        for (int i = 0; i < numR; i++)
          sf.N(rVals(i), N.col(i).data());
        //cout << "N\n" << N << endl;
      }
      u2eView = u2e*N;
      u2Tmp.col(iViewNode) = u2e.col(0);
      u2Tmp.block(0, iViewNode + 1, numDepVars, numR) = u2eView;
      iViewNode += numR + 1;
    }
    // finish the last view node
    u2Tmp.col(iViewNode) = u2e.col(1);
    MapVec u2ViewVector(u2Tmp.data(), u2Tmp.size());
    u.row(timeStep) = u2ViewVector;
  }
}

void PDESolution::print() const
{
  cout << "Solution, u\n" << u << endl;
}
