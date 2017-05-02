#include <iostream>
using std::cout;
using std::endl;

#include "PDEModel.h"
#include "ShapeFunctionManager.h"


PDEModel::PDEModel(const RealVector &mesh, int polyOrder, size_t numDofsPerNode,
  ShapeFunctionManager &sfm) :
origMesh(mesh), numDofsPerNode(numDofsPerNode),
sfm(sfm)
{
  size_t numElems = mesh.size()-1;
  elements.resize(numElems);
  elementDofOffsets.resize(numElems);
  const ShapeFunctionManager::EvaluatedSF &esf =
    sfm.getShapeFunction(polyOrder);
  int dof = 0;
  for (int i = 0; i < numElems; i++) {
    elements[i] = PDEElement(i, i + 1, &esf);
    elementDofOffsets[i] = dof;
    dof += polyOrder;
  }
  numNodesFEEqns_ = (dof + 1);
  numEqns = numDofsPerNode*numNodesFEEqns_;
}


PDEModel::~PDEModel()
{
}

void PDEModel::getDofIndicesForElem(int elemIndex,
  DofList &dofs) const
{
  const PDEElement &elem = elements[elemIndex];
  const ShapeFunctionManager::EvaluatedSF &esf = elem.getSF();
  int nn = esf.getShapeFunction().numNodes();
  dofs.resize(nn);
  int d = elementDofOffsets[elemIndex];
  dofs[0] = d;
  dofs[1] = d + nn - 1;
  for (int i = 2; i < nn; i++)
    dofs[i] = ++d;
}

void PDEModel::globalToElemVec(const PDEModel::DofList &eDofs, 
  const RealMatrix &ug, RealMatrix &ue) {
  size_t n = eDofs.size();
  ue.resize(ue.rows(), n);
  for (size_t i = 0; i < n; i++)
    ue.col(i) = ug.col(eDofs[i]);
}