// Copyright (C) 2016-2017 William H. Greene
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, see <http://www.gnu.org/licenses/>.

#include <iostream>
using std::cout;
using std::endl;
#include <algorithm>
#include <limits>

#include "PDEMeshMapper.h"
#include "PDE1dException.h"
#include "PDEModel.h"
#include "util.h"

#define DEBUGPRT 0

namespace {
  double calcXi(double xim1, double xi, double x) {
    return 2 * (x - xim1) / (xi - xim1) - 1;
  }
}


PDEMeshMapper::PDEMeshMapper(const RealVector &srcMesh, const PDEModel &model,
  const RealVector &destMesh) :
  srcMesh(srcMesh), destMesh(destMesh), model(model)
{
  const size_t numDest = destMesh.size();
  const size_t numSrc = srcMesh.size();
  destMeshParamVals.resize(numDest);
  destMeshElemIndex.resize(numDest);
  const int maxElemNodes = ShapeFunctionHierarchical::MAX_DEGREE + 1;
  elemDofs.resize(maxElemNodes);
  double tol = 100 * std::numeric_limits<double>::epsilon();
  for (int i = 0; i < numDest; i++) {
    double xd = destMesh[i];
    const double *begin = &srcMesh[0];
    const double *end = &srcMesh[numSrc - 1] + 1;
    const double *lwr = std::lower_bound(begin, end, xd);
    size_t indRight;
    size_t ind = lwr - begin;
#if DEBUGPRT
    pdePrintf("dest val=%20.16e, ind=%d\n", xd, ind);
#endif
    if (lwr == begin) {
      if ((srcMesh[0] - xd) > tol) {
        char msg[128];
        sprintf(msg, "ODE coupling point at %12.3e is less than left-most mesh point, %12.3e\n",
          xd, srcMesh[0]);
        throw PDE1dException("pde1d:invalid_map_point", msg);
      }
      indRight = 1;
    }
    else if (lwr == end) {
      if (xd - srcMesh[numSrc - 1] > tol) {
        char msg[128];
        sprintf(msg, "ODE coupling point at %12.3e is greater than right-most mesh point, %12.3e\n",
          xd, srcMesh[numSrc - 1]);
        throw PDE1dException("pde1d:invalid_map_point", msg);
      }
      indRight = numSrc - 1;
    }
    else {
      indRight = ind;
    }
    double s = calcXi(srcMesh[indRight - 1], srcMesh[indRight], xd);
#if DEBUGPRT
    pdePrintf("ind=%d, indRight=%d, s=%12.3e\n", ind, indRight, s);
#endif
    destMeshParamVals[i] = s;
    int eInd = static_cast<int>(indRight - 1);
    destMeshElemIndex[i] = eInd;
    model.getDofIndicesForElem(eInd, elemDofs);
    srcMeshDofIndices.insert(srcMeshDofIndices.end(), elemDofs.begin(), elemDofs.end());
  }
  std::sort(srcMeshDofIndices.begin(), srcMeshDofIndices.end());
  srcMeshDofIndices.erase(std::unique(srcMeshDofIndices.begin(), 
    srcMeshDofIndices.end()), srcMeshDofIndices.end());
}

void PDEMeshMapper::mapFunction(const RealMatrix &srcU, RealMatrix &destU)
{
  mapFunctionImpl(srcU, destU, false);
}

void PDEMeshMapper::mapFunctionDer(const RealMatrix &srcU, RealMatrix &destU)
{
  mapFunctionImpl(srcU, destU, true);
}

void PDEMeshMapper::mapFunctionImpl(const RealMatrix &srcU, RealMatrix &destU,
  bool calcDeriv)
{
  const size_t numDepVars = srcU.rows();
  const size_t numDest = destMesh.size();
  destU.resize(numDepVars, numDest);
  destU.setZero();
  RealVector N;
  for (int i = 0; i < numDest; i++) {
    double s = destMeshParamVals[i];
    int eInd = destMeshElemIndex[i];
    const PDEElement &elem = model.element(eInd);
    const ShapeFunction &sf = elem.getSF().getShapeFunction();
    model.getDofIndicesForElem(eInd, elemDofs);
#if 0
    pdePrintf("eInd=%d, nn=%d, dofs=%d %d\n", eInd, sf.numNodes(),
      elemDofs[0], elemDofs[1]);
#endif
    N.resize(sf.numNodes());
    double jac = 1;
    if (calcDeriv) {
      sf.dNdr(s, N.data());
      jac = 2. / (srcMesh[eInd+1]-srcMesh[eInd]);
    }
    else
      sf.N(s, N.data());
    for (int j = 0; j < sf.numNodes(); j++)
      destU.col(i) += srcU.col(elemDofs[j])*N[j];
    destU.col(i) *= jac;
  }
}
