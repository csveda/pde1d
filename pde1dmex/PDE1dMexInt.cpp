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
#include <stdio.h>
#include <cstdarg>

using std::cout;
using std::endl;

#ifdef _MSC_VER
// octave incorrectly defines mwSize
#pragma warning( push )
#pragma warning( disable : 4267)
#endif

#include "PDE1dMexInt.h"
#include "MexInterface.h"
#include "PDE1dException.h"

void pdePrintf(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  char msg[4096];
  vsprintf(msg, format, ap);
  va_end(ap);
  // matlab mex connects cout to console but not printf
  //std::cout << msg;
  mexPrintf(msg);
}

namespace {

  void print(const mxArray *a, const char *name)
  {
    size_t m = mxGetM(a);
    size_t n = mxGetN(a);
    const Eigen::Map<Eigen::MatrixXd> A(mxGetPr(a), m, n);
    mexPrintf("%s(%d,%d)\n", name, m, n);
    for (size_t i = 0; i < m; i++) {
      for (size_t j = 0; j < n; j++)
        mexPrintf("%g ", A(i, j));
      mexPrintf("\n");
    }
  }

}

PDE1dMexInt::PDE1dMexInt(int m, const mxArray *pdefun, const mxArray *icfun,
  const mxArray *bcfun,
  const mxArray *xmesh, const mxArray *tspan) : mCoord(m), pdefun(pdefun),
  icfun(icfun), bcfun(bcfun), xmesh(xmesh), tspan(tspan)
{
  mxX1 = mxX2 = mxT = mxVec1 = mxVec2  = 0;
  size_t numMesh = mxGetNumberOfElements(xmesh);
  mesh.resize(numMesh);
  std::copy_n(mxGetPr(xmesh), numMesh, mesh.data());
  size_t numTime = mxGetNumberOfElements(tspan);

  tSpan.resize(numTime);
  std::copy_n(mxGetPr(tspan), numTime, tSpan.data());
  mxX1 = mxCreateDoubleScalar(0);
  mxX2 = mxCreateDoubleScalar(0);
  mxT = mxCreateDoubleScalar(0);
  setNumPde();
  mxVec1 = mxCreateDoubleMatrix(numPDE, 1, mxREAL);
  mxVec2 = mxCreateDoubleMatrix(numPDE, 1, mxREAL);
  mxMat1 = mxCreateDoubleMatrix(numPDE, numMesh, mxREAL);
  mxMat2 = mxCreateDoubleMatrix(numPDE, numMesh, mxREAL);
  odefun = 0;
  odeIcFun = 0;
  odeMesh = 0;
  numODE = 0;
  mxV = 0;
  mxVDot = 0;
  mxOdeU = 0;
  mxOdeDuDx = 0;
  mxOdeR = 0;
  mxOdeDuDt = 0;
  mxOdeDuDxDt = 0;
  eventsFun = 0;
  numEvents = 0;
  mxM = 0;
  mxEventsU = 0;
}


PDE1dMexInt::~PDE1dMexInt()
{
  mxDestroyArray(mxX1);
  mxDestroyArray(mxX2);
  mxDestroyArray(mxT);
  mxDestroyArray(mxVec1);
  mxDestroyArray(mxVec2);
  destroy(mxMat1);
  destroy(mxMat2);
  destroy(mxV);
  destroy(mxVDot);
  destroy(mxOdeU);
  destroy(mxOdeDuDx);
  destroy(mxOdeR);
  destroy(mxOdeDuDt);
  destroy(mxOdeDuDxDt);
  destroy(mxM);
  destroy(mxEventsU);
}

void PDE1dMexInt::setODEDefn(const mxArray *odeFun, const mxArray *icFun,
  const mxArray *odemesh)
{
  odefun = odeFun;
  odeIcFun = icFun;
  odeMesh = odemesh;
  setNumOde();
  size_t numXi = mxGetNumberOfElements(odeMesh);
  mxV = mxCreateDoubleMatrix(numODE, 1, mxREAL);
  mxVDot = mxCreateDoubleMatrix(numODE, 1, mxREAL);
  mxOdeU = mxCreateDoubleMatrix(numXi, 1, mxREAL);
  mxOdeDuDx = mxCreateDoubleMatrix(numXi, 1, mxREAL);
  mxOdeR = mxCreateDoubleMatrix(numXi, 1, mxREAL);
  mxOdeDuDt = mxCreateDoubleMatrix(numXi, 1, mxREAL);
  mxOdeDuDxDt = mxCreateDoubleMatrix(numXi, 1, mxREAL);
  odeMeshVec = MexInterface::fromMxArrayVec(odeMesh);
}

void PDE1dMexInt::setEventsFunction(const mxArray *eventsFun) {
  if (!eventsFun) return;
  this->eventsFun = eventsFun;
  setNumEvents();
  mwSize nn = static_cast<mwSize>(mesh.size());
  mxEventsU = mxCreateDoubleMatrix(nn*numPDE, 1, mxREAL);
}

void PDE1dMexInt::evalIC(double x, RealVector &ic)
{
  setScalar(x, mxX1);
  const int nargout = 1, nargin = 2;
  const mxArray *funcInp[] = { icfun, mxX1 };
  RealVector *outArgs[] = { &ic };
  callMatlab(funcInp, nargin, outArgs, nargout);
}

void PDE1dMexInt::evalODEIC(RealVector &ic)
{
  const int nargout = 1, nargin = 1;
  const mxArray *funcInp[] = { odeIcFun };
  RealVector *outArgs[] = { &ic };
  callMatlab(funcInp, nargin, outArgs, nargout);
}

void PDE1dMexInt::evalBC(double xl, const RealVector &ul,
  double xr, const RealVector &ur, double t, 
  const RealVector &v, const RealVector &vDot, BC &bc) {
  setScalar(xl, mxX1);
  setVector(ul, mxVec1);
  setScalar(xr, mxX2);
  setVector(ur, mxVec2);
  setScalar(t, mxT);
  const int nargout = 4;
  int nargin = 6;
  if (numODE) {
    nargin = 8;
    setVector(v, mxV);
    setVector(vDot, mxVDot);
  }
  const mxArray *funcInp[] = { bcfun, mxX1, mxVec1, mxX2, mxVec2, mxT,
    mxV, mxVDot };
  RealVector *outArgs[] = { &bc.pl, &bc.ql, &bc.pr, &bc.qr };
  callMatlab(funcInp, nargin, outArgs, nargout);
}

void PDE1dMexInt::evalPDE(double x, double t,
  const RealVector &u, const RealVector &DuDx, 
  const RealVector &v, const RealVector &vDot, PDECoeff &pde) {
  // Evaluate pde coefficients one point at a time
  // [c,f,s] = heatpde(x,t,u,DuDx)
  setScalar(x, mxX1);
  setScalar(t, mxT);
  setVector(u, mxVec1);
  setVector(DuDx, mxVec2);
  const int nargout = 3;
  int nargin = 5;
  if (numODE) {
    nargin = 7;
    setVector(v, mxV);
    setVector(vDot, mxVDot);
  }
  const mxArray *funcInp[] = { pdefun, mxX1, mxT, mxVec1, mxVec2,
    mxV, mxVDot };
  callMatlab(funcInp, nargin, nargout);
  mxArray* c = matOutArgs[0];

  if (mxGetM(c) == numPDE && mxGetN(c) == numPDE)
    pde.c.resize(numPDE, numPDE); // handle optional coupled mass matrix
  processReturnedArg(pdefun, 0, c, &pde.c);
  processReturnedArg(pdefun, 1, matOutArgs[1], &pde.f);
  processReturnedArg(pdefun, 2, matOutArgs[2], &pde.s);
  for (int i = 0; i < nargout; i++)
    destroy(matOutArgs[i]);
}

void PDE1dMexInt::evalPDE(const RealVector &x, double t,
  const RealMatrix &u, const RealMatrix &DuDx, 
  const RealVector &v, const RealVector &vDot, PDECoeff &pde)
{
  // Evaluate pde coefficients at all x-locations
  // [c,f,s] = heatpde(x,t,u,DuDx)
  setMatrix(x.transpose(), mxX1);
  setScalar(t, mxT);
  setMatrix(u, mxMat1);
  setMatrix(DuDx, mxMat2);
  const int nargout = 3;
  int nargin = 5;
  if (numODE) {
    nargin = 7;
    setVector(v, mxV);
    setVector(vDot, mxVDot);
  }
  const mxArray *funcInp[] = { pdefun, mxX1, mxT, mxMat1, mxMat2,
  mxV, mxVDot};
  RealMatrix *outArgs[] = { &pde.c, &pde.f, &pde.s };
  callMatlab(funcInp, nargin, outArgs, nargout);
}

void PDE1dMexInt::evalODE(double t, const RealVector &v,
  const RealVector &vdot, const RealMatrix &u, const RealMatrix &DuDx,
  const RealMatrix &odeR, const RealMatrix &odeDuDt,
  const RealMatrix &odeDuDxDt, RealVector &f)
{
  // odeFunc(t,v,vdot,x,u,DuDx)
  setScalar(t, mxT);
  setVector(v, mxV);
  setVector(vdot, mxVDot);
  setMatrix(u, mxOdeU);
  setMatrix(DuDx, mxOdeDuDx);
  setMatrix(odeR, mxOdeR);
  setMatrix(odeDuDt, mxOdeDuDt);
  setMatrix(odeDuDxDt, mxOdeDuDxDt);
  const int nargout = 1;
  const int nargin = 10;
  const mxArray *funcInp[] = { odefun, mxT, mxV, mxVDot, odeMesh,
    mxOdeU, mxOdeDuDx, mxOdeR, mxOdeDuDt, mxOdeDuDxDt };
  RealVector *outArgs[] = { &f };
  callMatlab(funcInp, nargin, outArgs, nargout);
}

void PDE1dMexInt::evalEvents(double t, const RealMatrix &u,
  RealVector &eventsVal, RealVector &eventsIsTerminal, 
  RealVector &eventsDirection)
{
  // [value,isterminal,direction] = events(m,t,xmesh,umesh)
  eventsIsTerminal.setZero();
  eventsDirection.setZero();
  setScalar(t, mxT);
  setMatrix(u, mxEventsU);
  const mxArray *funcInp[] = { eventsFun, mxM, mxT, xmesh, mxEventsU };
  RealVector *outArgs[] = {&eventsVal, &eventsIsTerminal, &eventsDirection};
  const int nargout = 3;
  const int nargin = 5;
  callMatlab(funcInp, nargin, outArgs, nargout);
}

const RealVector &PDE1dMexInt::getMesh() const {
  return mesh;
}

const RealVector &PDE1dMexInt::getODEMesh()
{
  return odeMeshVec;
}

const RealVector &PDE1dMexInt::getTimeSpan() const { return tSpan; }

void PDE1dMexInt::callMatlab(const mxArray *inArgs[], int nargin, int nargout) {
  std::fill_n(matOutArgs, nargout, nullptr);
  int err = mexCallMATLAB(nargout, matOutArgs, nargin,
    const_cast<mxArray**>(inArgs), "feval");
  if (err) {
    char msg[1024];
    std::string funcName = getFuncNameFromHandle(inArgs[0]);
    sprintf(msg, "An error occurred in the call to user-defined function:\n\"%s\".",
      funcName.c_str());
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB:err", msg);
  }
}

void PDE1dMexInt::callMatlab(const mxArray *inArgs[], int nargin,
  RealVector *outArgs[], int nargout)
{
  callMatlab(inArgs, nargin, nargout);
  for (int i = 0; i < nargout; i++) {
    mxArray *a = matOutArgs[i];
    processReturnedArg(inArgs[0], i, a, outArgs[i]);
    destroy(a);
  }
}

void PDE1dMexInt::callMatlab(const mxArray *inArgs[], int nargin,
  RealMatrix *outArgs[], int nargout)
{
  callMatlab(inArgs, nargin, nargout);
  for (int i = 0; i < nargout; i++) {
    mxArray *a = matOutArgs[i];
    if (!a)
      pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB:arg", "Error in mexCallMATLAB arg.");
    processReturnedArg(inArgs[0], i, a, outArgs[i]);
    destroy(a);
  }
}

#if 0
void PDE1dMexInt::callMatlab(const mxArray *inArgs[], int nargin, int nargout)
{
  callMatlab(inArgs, nargin);
  for (int i = 0; i < nargout; i++) {
    mxArray *a = matOutArgs[i];
    if (!a)
      pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB:arg", "Error in mexCallMATLAB arg.");
  }
}
#endif

void PDE1dMexInt::checkMxType(const mxArray *a, int argIndex, const mxArray *funcHandle) {

  if (!mxIsDouble(a)) {
    char msg[1024];
    std::string funcName = getFuncNameFromHandle(funcHandle);
    sprintf(msg, "In the call to user-defined function, \"%s\",\n"
      "returned entry %d was not a double-precision, floating-point array.", 
      funcName.c_str(), argIndex + 1);
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB:argNotNumeric", msg);
  }
  if (mxIsComplex(a)) {
    char msg[1024];
    std::string funcName = getFuncNameFromHandle(funcHandle);
    sprintf(msg, "In the call to user-defined function, \"%s\",\n"
      "returned entry %d was complex.\n"
      "Complex equations are not currently supported by pde1d.",
      funcName.c_str(), argIndex + 1);
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB:argIsComplex", msg);
  }

}

template<typename T>
void PDE1dMexInt::processReturnedArg(const mxArray* callingFunc,
  int argNum, const mxArray* retArg, T* outArg) {
  if (!retArg)
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB:arg", "Error in mexCallMATLAB arg.");
  size_t retRows = mxGetM(retArg);
  size_t retCols = mxGetN(retArg);
  size_t exRows = outArg->rows();
  size_t exCols = outArg->cols();
  bool dimsOK = false;
  bool isVec = exCols == 1;
  if (isVec)
    dimsOK = (retRows==1 || retCols==1) && (
      retRows * retCols == exRows * exCols);
  else
    dimsOK = retRows == exRows && retCols == exCols;
  if (! dimsOK) {
    char msg[1024];
    std::string funcName = getFuncNameFromHandle(callingFunc);
    size_t m = mxGetM(retArg), n = mxGetN(retArg);
    sprintf(msg, "In the call to user-defined function:\n\"%s\"\n"
      "returned entry %d had size (%zd x %zd) but a matrix of size (%zd x %zd)"
      " was expected.", funcName.c_str(), argNum + 1, retRows, retCols,
      exRows, exCols);
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB:arglen", msg);
  }
  checkMxType(retArg, argNum, callingFunc);
  std::copy_n(mxGetPr(retArg), retRows*retCols, outArg->data());
}


void PDE1dMexInt::setNumPde() {
  setScalar(mesh(0), mxX1);
  mxArray *initCond[1];
  const mxArray *funcInp[] = { icfun, mxX1 };
  int err = mexCallMATLAB(1, initCond, 2,
    const_cast<mxArray**>(funcInp), "feval");
  if (err)
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB",
      "Error in mexCallMATLAB.\n");
  numPDE = mxGetNumberOfElements(initCond[0]);
  if (numPDE <= 0) {
    char msg[1024];
    std::string funcName = getFuncNameFromHandle(icfun);
    sprintf(msg, "An error occurred in the call to user-defined function, \"%s\"."
      "Returned matrix had zero-length.", funcName.c_str());
    pdeErrMsgIdAndTxt("pde1d:icFuncIllegal", msg);
  }
  destroy(initCond[0]);
}

void PDE1dMexInt::setNumOde()
{
  mxArray *initCond[1];
  const mxArray *funcInp[] = { odeIcFun };
  int err = mexCallMATLAB(1, initCond, 1,
    const_cast<mxArray**>(funcInp), "feval");
  if (err)
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB",
    "Error in mexCallMATLAB.\n");
  numODE = mxGetNumberOfElements(initCond[0]);
  destroy(initCond[0]);
  //printf("numODE=%d\n", numODE);
}

void PDE1dMexInt::setNumEvents()
{
  //  [value,isterminal,direction] = events(m,t,xmesh,umesh)
  mxM = mxCreateDoubleScalar(0);
  int *mxMptr = reinterpret_cast<int*>(mxGetPr(mxM));
  mxMptr[0] = mCoord;
  setScalar(tSpan[0], mxT);
  const size_t numMesh = mesh.size();
  RealMatrix u(numPDE, numMesh);
  // get initial conditions at all points in the mesh
  mxArray *initCond = 0;
  const mxArray *funcInpIC[] = { icfun, mxX1 };
  double *mxXptr = mxGetPr(mxX1);
  for (int i = 0; i < numMesh; i++) {
    *mxXptr = mesh[i];
    int err = mexCallMATLAB(1, &initCond, 2,
      const_cast<mxArray**>(funcInpIC), "feval");
    if (err)
      pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB",
        "Error in mexCallMATLAB.\n");
    int numRet = mxGetNumberOfElements(initCond);
    if (numRet != numPDE) {
      char msg[1024];
      std::string funcName = getFuncNameFromHandle(icfun);
      sprintf(msg, "Function \"%s\" returned a vector of length %d but a vector "
        " of length %d was expected.", funcName.c_str(), numRet, numPDE);
    }
    std::copy_n(mxGetPr(initCond), numRet, u.col(i).data());
    destroy(initCond);
  }
  setMatrix(u, mxVec1);
  const mxArray *funcInp[] = { eventsFun, mxM, mxT, xmesh, mxVec1 };
  int nargin = sizeof(funcInp) / sizeof(funcInp[0]);
  int nargout = 3;
  int err = mexCallMATLAB(nargout, matOutArgs, 
    nargin, const_cast<mxArray**>(funcInp), "feval");
  if (err)
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB",
      "Error in mexCallMATLAB.\n");
  if (!matOutArgs[0]) {
    char msg[1024];
    std::string funcName = getFuncNameFromHandle(eventsFun);
    sprintf(msg, "Events function \"%s\" must return at least one argument.",
      funcName.c_str());
    pdeErrMsgIdAndTxt("pde1d:eventsFuncIllegal", msg);
  }
  numEvents = mxGetNumberOfElements(matOutArgs[0]);
  for (int i = 1; i < nargout; i++) {
    mxArray *ai = matOutArgs[i];
    if (ai && mxGetNumberOfElements(ai) != numEvents) {
      char msg[1024];
      std::string funcName = getFuncNameFromHandle(eventsFun);
      sprintf(msg, "The lengths of all vectors returned from events function \"%s\""
        " must be the same.", funcName.c_str());
      pdeErrMsgIdAndTxt("pde1d:eventsFuncIllegal", msg);
    }
  }
}

std::string PDE1dMexInt::getFuncNameFromHandle(const mxArray *fh)
{
  mxArray *funcName = 0;
  int err = mexCallMATLAB(1, &funcName, 1,
    const_cast<mxArray**>(&fh), "func2str");
  if (err)
    pdeErrMsgIdAndTxt("pde1d:mexCallMATLAB",
    "Error in mexCallMATLAB.\n");
  const int bufLen = 1024;
  char buf[bufLen];
  int len = mxGetString(funcName, buf, bufLen);
  std::string nam(buf);
  return nam;
}

#ifdef _MSC_VER
#pragma warning( pop )
#endif