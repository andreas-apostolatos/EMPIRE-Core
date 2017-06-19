/*  Copyright &copy; 2013, TU Muenchen, Chair of Structural Analysis,
 *  Fabien Pean, Andreas Apostolatos, Chenshen Wu,
 *  Ragnar Björnsson, Stefan Sicklinger, Tianyang Wang, Munich
 *
 *  All rights reserved.
 *
 *  This file is part of EMPIRE.
 *
 *  EMPIRE is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  EMPIRE is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with EMPIRE.  If not, see http://www.gnu.org/licenses/.
 */

#include "IGAMortarMapper.h"
#include "IGAPatchSurface.h"
#include "IGAMesh.h"
#include "FEMesh.h"
#include "ClipperAdapter.h"
#include "TriangulatorAdaptor.h"
#include "MathLibrary.h"
#include "DataField.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <stdlib.h>
#include <math.h>
#include <set>
#include <algorithm>
#include "IGAMortarCouplingMatrices.h"

using namespace std;

namespace EMPIRE {

/// Declaration statement
static const string HEADER_DECLARATION = "Author: Andreas Apostolatos";

IGAMortarMapper::IGAMortarMapper(std::string _name, IGAMesh *_meshIGA, FEMesh *_meshFE,
                                 bool _isMappingIGA2FEM) :
    name(_name), meshIGA(_meshIGA), isMappingIGA2FEM(_isMappingIGA2FEM) {

    assert(_meshIGA != NULL);
    assert(_meshFE != NULL);
    assert(_meshIGA->type == EMPIRE_Mesh_IGAMesh);
    assert(_meshFE->type == EMPIRE_Mesh_FEMesh);

    if (_meshFE->triangulate() == NULL)
        meshFE = _meshFE;
    else
        meshFE = _meshFE->triangulate();

    mapperType = EMPIRE_IGAMortarMapper;

    projectedCoords.resize(meshFE->numNodes);
    projectedPolygons.resize(meshFE->numElems);
    triangulatedProjectedPolygons.resize(meshFE->numElems);

    if (isMappingIGA2FEM) {
        numNodesSlave = meshIGA->getNumNodes();
        numNodesMaster = meshFE->numNodes;
    } else {
        numNodesSlave = meshFE->numNodes;
        numNodesMaster = meshIGA->getNumNodes();
    }

    // Initialize flag for the case that coupling between the patches is enables and when Dirichlet boundary conditions are applied in not all directions
    useIGAPatchCouplingPenalties = false;
    
    // Initialize coupling matrices
    couplingMatrices = new IGAMortarCouplingMatrices(numNodesMaster , numNodesSlave);

    setParametersProjection();
    setParametersNewtonRaphson();
    setParametersNewtonRaphsonBoundary();
    setParametersBisection();
    setParametersIntegration();
    setParametersIgaPatchCoupling();
    setParametersDirichletBCs();
}

IGAMortarMapper::~IGAMortarMapper() {

    for (int i = 0; i < meshFE->numElems; i++)
        delete[] meshFEDirectElemTable[i];
    delete[] meshFEDirectElemTable;
    delete gaussTriangle;
    delete gaussQuad;
    delete couplingMatrices;
}

void IGAMortarMapper::setParametersIntegration(int _numGPTriangle, int _numGPQuad) {
    integration.numGPTriangle=_numGPTriangle;
    integration.numGPQuad=_numGPQuad;
}

void IGAMortarMapper::setParametersNewtonRaphson(int _maxNumOfIterations, double _tolerance) {
    newtonRaphson.maxNumOfIterations=_maxNumOfIterations;
    newtonRaphson.tolerance=_tolerance;
}

void IGAMortarMapper::setParametersNewtonRaphsonBoundary(int _maxNumOfIterations, double _tolerance) {
    newtonRaphsonBoundary.maxNumOfIterations=_maxNumOfIterations;
    newtonRaphsonBoundary.tolerance=_tolerance;
}

void IGAMortarMapper::setParametersBisection(int _maxNumOfIterations, double _tolerance) {
    bisection.maxNumOfIterations=_maxNumOfIterations;
    bisection.tolerance=_tolerance;
}

void IGAMortarMapper::setParametersProjection(double _maxProjectionDistance, int _numRefinementForIntialGuess,
                                              double _maxDistanceForProjectedPointsOnDifferentPatches) {
    projectionProperties.maxProjectionDistance=_maxProjectionDistance;
    projectionProperties.numRefinementForIntialGuess=_numRefinementForIntialGuess;
    projectionProperties.maxDistanceForProjectedPointsOnDifferentPatches=_maxDistanceForProjectedPointsOnDifferentPatches;
}

void IGAMortarMapper::setParametersIgaPatchCoupling(double _dispPenalty, double _rotPenalty, int isAutomaticPenaltyFactors) {
    IgaPatchCoupling.dispPenalty = _dispPenalty;
    IgaPatchCoupling.rotPenalty = _rotPenalty;
    IgaPatchCoupling.isAutomaticPenaltyFactors = isAutomaticPenaltyFactors;
}

void IGAMortarMapper::setParametersDirichletBCs(int _isDirichletBCs) {
    dirichletBCs.isDirichletBCs = _isDirichletBCs;
}

void IGAMortarMapper::buildCouplingMatrices() {
    HEADING_OUT(3, "IGAMortarMapper", "Building coupling matrices for ("+ name +")...", infoOut);
    {
        int nIG = meshIGA->getNumNodes();
        int nFE = meshFE->numNodes;
        INFO_OUT() << "Number of nodes in NURBS mesh is " << nIG << endl;
        INFO_OUT() << "Number of nodes in FE mesh is    " << nFE << endl;
        INFO_OUT() << "Size of matrices will be " << (isMappingIGA2FEM?nFE:nIG) << "x" << (isMappingIGA2FEM?nFE:nIG) << " and "
                   << (isMappingIGA2FEM?nFE:nIG) << "x" << (isMappingIGA2FEM?nIG:nFE) << endl;
    }

    //Instantiate quadrature rules
    gaussTriangle = new MathLibrary::IGAGaussQuadratureOnTriangle(integration.numGPTriangle);
    gaussQuad = new MathLibrary::IGAGaussQuadratureOnQuad(integration.numGPQuad);

    //Set default scheme values
    IGAPatchSurface::MAX_NUM_ITERATIONS = newtonRaphson.maxNumOfIterations;
    IGAPatchSurface::TOL_ORTHOGONALITY = newtonRaphson.tolerance;

    // Compute the EFT for the FE mesh
    initTables();

    // Project the FE nodes onto the multipatch trimmed geometry
    projectPointsToSurface();

    //    // Initialize coupling matrices
    //    couplingMatrices = new IgaMortarCouplingMatrices(numNodesMaster , numNodesSlave);

    // Write the projected points on to a file to be used in MATLAB
    writeProjectedNodesOntoIGAMesh();

    // Reserve some space for gauss point values
    streamGP.reserve(8*meshFE->numElems*gaussQuad->numGaussPoints);

    // Compute CNN and CNR
    computeCouplingMatrices();

    if(Message::isDebugMode()) {
        writeGaussPointData(); // ONLY FOR L2 NORM COMPUTATION PURPOSE. TO ACTIVATE WITH CAUTION.
    }
    streamGP.clear();

    // Write polygon net of projected elements to a vtk file
    writeCartesianProjectedPolygon("trimmedPolygonsOntoNURBSSurface", trimmedProjectedPolygons);
    writeCartesianProjectedPolygon("integratedPolygonsOntoNURBSSurface", triangulatedProjectedPolygons2);
    trimmedProjectedPolygons.clear();
    triangulatedProjectedPolygons2.clear();
    
    if(IgaPatchCoupling.dispPenalty>0 || IgaPatchCoupling.rotPenalty>0)
        useIGAPatchCouplingPenalties = true;

    bool _isdirichletBCs = false;
    bool isClampedDofs = false;
    std::vector<int> clampedIDs;
    if(dirichletBCs.isDirichletBCs==1) {
        _isdirichletBCs = true;
        clampedIDs = meshIGA->getClampedDofs();

        int clampedDirections = meshIGA->getClampedDirections();
        if(clampedDirections == 1 || clampedDirections == 2)
            isClampedDofs = true;
    }

    couplingMatrices->setIsIGAPatchCoupling(useIGAPatchCouplingPenalties , isClampedDofs);

    if (useIGAPatchCouplingPenalties) {
        INFO_OUT()<<"Compute Penalty Patch Coupling"<<endl;
        computeIGAPatchWeakContinuityConditionMatrices();
    }
    else
        INFO_OUT()<<"No Penalty Patch Coupling"<<std::endl;

    if(isClampedDofs)                           // this is for MapperAdapter, this makes sure that the fields in all directions are sent at the same time
        useIGAPatchCouplingPenalties=true;      // if it is not always clamped in all three dircetions


    couplingMatrices->setIsDirichletBCs(_isdirichletBCs);

    if(dirichletBCs.isDirichletBCs==1)
        couplingMatrices->applyDirichletBCs(clampedIDs);
    else
        INFO_OUT()<<"No Diriclet Boundary Conditions"<<std::endl;

    //    // Remove empty rows and columns from system in case consistent mapping for the traction from FE Mesh to IGA multipatch geometry is required
    if(!isMappingIGA2FEM) {
        couplingMatrices->enforceCnn();
    }

    writeCouplingMatricesToFile();

    couplingMatrices->factorizeCorrectCNN();
    INFO_OUT()<<"Factorize was successful"<<std::endl;

    if(dirichletBCs.isDirichletBCs==0)
        checkConsistency();
}

void IGAMortarMapper::initTables() {
    /* using the map to store the nodeIDs
     * but here the "key" is the node ID, and the value is the position in nodeIDs
     * the map is sorted automatically, so it is efficient for searching
     */

    // compute direct element table for fluid mesh
    meshFEDirectElemTable = new int*[meshFE->numElems]; // deleted
    for (int i = 0; i < meshFE->numElems; i++)
        meshFEDirectElemTable[i] = new int[meshFE->numNodesPerElem[i]];

    map<int, int> meshFENodesMap;
    for (int i = 0; i < meshFE->numNodes; i++)
        meshFENodesMap.insert(meshFENodesMap.end(), pair<int, int>(meshFE->nodeIDs[i], i));
    int count = 0;

    for (int i = 0; i < meshFE->numElems; i++) {
        const int numNodesPerElem = meshFE->numNodesPerElem[i];

        for (int j = 0; j < numNodesPerElem; j++) {
            if (meshFENodesMap.find(meshFE->elems[count + j]) == meshFENodesMap.end()) {
                ERROR_OUT() << "Cannot find node ID " << meshFE->elems[count + j] << endl;
                exit(-1);
            }
            meshFEDirectElemTable[i][j] = meshFENodesMap.at(meshFE->elems[count + j]);
        }
        count += numNodesPerElem;
    }

    for (int node = 0; node < meshFE->numNodes; node++) {
        for (int elem = 0; elem < meshFE->numElems; elem++) {
            const int numNodesPerElem = meshFE->numNodesPerElem[elem];
            int* out = find(meshFEDirectElemTable[elem],meshFEDirectElemTable[elem]+numNodesPerElem,node);
            if(out != meshFEDirectElemTable[elem]+numNodesPerElem) {
                meshFENodeToElementTable[node].push_back(elem);
            }
        }
    }

}

void IGAMortarMapper::projectPointsToSurface() {
    // Time stamps
    time_t timeStart, timeEnd;

    // Initialization of variables

    // Array of booleans containing flags on the projection of the FE nodes onto the NURBS patch
    // A node needs to be projected at least once
    vector<bool> isProjected(meshFE->numNodes);
    // Keep track of the minimum distance found between a node and a patch
    vector<double> minProjectionDistance(meshFE->numNodes, 1e9);
    // Keep track of the point on patch related to minimum distance
    vector<vector<double> > minProjectionPoint(meshFE->numNodes);
    // List of patch to try a projection for every node
    vector<set<int> > patchToProcessPerNode(meshFE->numNodes);
    vector<set<int> > patchToProcessPerElement(meshFE->numElems);

    // Initial guess for projection onto the NURBS patch
    double initialU, initialV;

    // Initialize the array of the Cartesian coordinates of a node in the FE side
    double P[3];

    // Get the number of patches in the IGA mesh
    int numPatches = meshIGA->getNumPatches();
    // Bounding box preprocessing, assign to each node the patches to be visited
    INFO_OUT()<<"Bounding box preprocessing..."<<endl;
    time(&timeStart);
    for (int i = 0; i < meshFE->numNodes; i++) {
        double P[3];
        P[0] = meshFE->nodes[3 * i + 0];
        P[1] = meshFE->nodes[3 * i + 1];
        P[2] = meshFE->nodes[3 * i + 2];
        for(int patchCount = 0; patchCount < numPatches; patchCount++) {
            IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(patchCount);
            bool isInside = thePatch->getBoundingBox().isPointInside(P, projectionProperties.maxProjectionDistance);
            if(isInside)
                patchToProcessPerNode[i].insert(patchCount);
        }
        if(patchToProcessPerNode[i].empty()) {
            stringstream msg;
            msg << "Node [" << i << "] is not in any bounding box of NURBS patches ! Increase maxProjectionDistance !";
            ERROR_BLOCK_OUT("IGAMortarMapper", "projectPointsToSurface", msg.str());
        }
    }
    time(&timeEnd);
    INFO_OUT()<<"Bounding box preprocessing done in "<< difftime(timeEnd, timeStart) << " seconds."<<endl;
    // Project the node for every patch's bounding box the node lies into
    // or on every patch if not found in a single bounding box
    INFO_OUT()<<"First pass projection..."<<endl;
    time(&timeStart);
    for(int i = 0; i < meshFE->numElems; i++) {
        int numNodesInElem = meshFE->numNodesPerElem[i];
        for(int patchIndex = 0; patchIndex < numPatches; patchIndex++) {
            IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(patchIndex);
            bool initialGuessComputed = false;
            for(int j = 0; j < numNodesInElem; j++) {
                int nodeIndex = meshFEDirectElemTable[i][j];
                // If already projected, go to next node
                if(projectedCoords[nodeIndex].find(patchIndex) != projectedCoords[nodeIndex].end())
                    continue;
                // If node in BBox of patch
                if(patchToProcessPerNode[nodeIndex].find(patchIndex) != patchToProcessPerNode[nodeIndex].end()) {
                    if(!initialGuessComputed) {
                        computeInitialGuessForProjection(patchIndex, i, nodeIndex, initialU, initialV);
                        initialGuessComputed = true;
                    }
                    bool flagProjected = projectPointOnPatch(patchIndex, nodeIndex, initialU, initialV, minProjectionDistance[nodeIndex], minProjectionPoint[nodeIndex]);
                    isProjected[nodeIndex] = isProjected[nodeIndex] || flagProjected;
                }
            }
        }
    }
    time(&timeEnd);
    INFO_OUT()<<"First pass projection done in "<< difftime(timeEnd, timeStart) << " seconds."<<endl;
    int missing = 0;
    for (int i = 0; i < meshFE->numNodes; i++) {
        if(!isProjected[i]) {
            missing++;
            WARNING_OUT()<<"Node not projected at first pass ["<<i<<"] of coordinates "<<meshFE->nodes[3*i]<<","<<meshFE->nodes[3*i+1]<<","<<meshFE->nodes[3*i+2]<<endl;
        }
    }
    INFO_OUT()<<meshFE->numNodes - missing << " nodes over " << meshFE->numNodes <<" could be projected during first pass." << endl;
    double initialTolerance = newtonRaphson.tolerance;

    // Second pass projection --> relax Newton-Rapshon tolerance and if still fails refine the sampling points for the Newton-Raphson initial guesses
    if(missing) {
        INFO_OUT()<<"Second pass projection..."<<endl;
        time(&timeStart);
        missing = 0;
        for (int i = 0; i < meshFE->numNodes; i++) {
            if(!isProjected[i]) {
                newtonRaphson.tolerance = 10*newtonRaphson.tolerance;
                for(set<int>::iterator patchIndex=patchToProcessPerNode[i].begin();patchIndex!=patchToProcessPerNode[i].end();patchIndex++) {
                    IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(*patchIndex);
                    computeInitialGuessForProjection(*patchIndex, meshFENodeToElementTable[i][0], i, initialU, initialV);
                    bool flagProjected = projectPointOnPatch(*patchIndex, i, initialU, initialV, minProjectionDistance[i], minProjectionPoint[i]);
                    isProjected[i] = isProjected[i] || flagProjected;
                }
                if(!isProjected[i]) {
                    for(set<int>::iterator patchIndex=patchToProcessPerNode[i].begin();patchIndex!=patchToProcessPerNode[i].end();patchIndex++) {
                        bool flagProjected = forceProjectPointOnPatch(*patchIndex, i, minProjectionDistance[i], minProjectionPoint[i]);
                        isProjected[i] = isProjected[i] || flagProjected;
                    }
                }
            }
            if(!isProjected[i]) {
                ERROR_OUT()<<"Node not projected at second pass ["<<i<<"] of coordinates "<<meshFE->nodes[3*i]<<","<<meshFE->nodes[3*i+1]<<","<<meshFE->nodes[3*i+2]<<endl;
                missing++;
            }
            newtonRaphson.tolerance = initialTolerance;
        }
        newtonRaphson.tolerance = initialTolerance;
        time(&timeEnd);
        INFO_OUT()<<"Second pass projection done! It took "<< difftime(timeEnd, timeStart) << " seconds."<<endl;
        if(missing) {
            stringstream msg;
            msg << missing << " nodes over " << meshFE->numNodes << " could NOT be projected during second pass !" << endl;
            msg << "Treatment possibility 1." << endl;
            msg << "Possibly relax parameters in projectionProperties or newtonRaphson" << endl;
            msg << "Treatment possibility 2." << endl;
            msg << "Remesh with higher accuracy on coordinates of the FE nodes, i.e. more digits" << endl;
            ERROR_BLOCK_OUT("IGAMortarMapper", "ProjectPointsToSurface", msg.str());
        }
    }
}

void IGAMortarMapper::computeInitialGuessForProjection(const int _patchIndex, const int _elemIndex, const int _nodeIndex, double& _u, double& _v) {

    IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(_patchIndex);
    /// 1iii.1. Initialize the flag to false and the node id to zero
    bool isNodeInsideElementProjected = false;
    int projectedNode = -1;
    /// 1iii.2. Loop over all nodes of the current element to check if there exist one node has been projected already
    for (int j = 0; j < meshFE->numNodesPerElem[_elemIndex]; j++) {
        int nodeIndex = meshFEDirectElemTable[_elemIndex][j];
        if (projectedCoords[nodeIndex].find(_patchIndex) != projectedCoords[nodeIndex].end()) {
            /// 1iii.2i. If the node has already been projected set projection flag to true
            isNodeInsideElementProjected = true;
            /// 1iii.2ii. Get the global ID of the projected node
            projectedNode = nodeIndex;
            /// 1iii.2iii. Break the loop
            break;
        }
    }
    /// 1iii.3. Check if there exist one node in the current element has been successfully projected
    if (isNodeInsideElementProjected) {
        /// 1iii.3i. If so, use result of the projected node as the initial guess for the projection step
        _u = projectedCoords[projectedNode][_patchIndex][0];
        _v = projectedCoords[projectedNode][_patchIndex][1];
    } else {
        /// 1iii.3ii. Otherwise, find the nearest knot intersection as initial guess for the projection step
        // Get the Cartesian coordinates of that input node
        double P[3];
        P[0] = meshFE->nodes[_nodeIndex * 3 + 0];
        P[1] = meshFE->nodes[_nodeIndex * 3 + 1];
        P[2] = meshFE->nodes[_nodeIndex * 3 + 2];
        // Get accordingly an initial guess for the projection onto the NURBS patch
        thePatch->findInitialGuess4PointProjection(_u, _v, P, projectionProperties.numRefinementForIntialGuess, projectionProperties.numRefinementForIntialGuess);
    }
}

bool IGAMortarMapper::projectPointOnPatch(const int patchIndex, const int nodeIndex, const double u0, const double v0, double& minProjectionDistance, vector<double>& minProjectionPoint) {

    IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(patchIndex);
    /// Get the Cartesian coordinates of the node in the FE side
    double P[3], projectedP[3];
    projectedP[0] = P[0] = meshFE->nodes[nodeIndex * 3 + 0];
    projectedP[1] = P[1] = meshFE->nodes[nodeIndex * 3 + 1];
    projectedP[2] = P[2] = meshFE->nodes[nodeIndex * 3 + 2];
    /// Get an initial guess for the parametric location of the projected node of the FE side on the NURBS patch
    double u = u0;
    double v = v0;
    /// Compute point projection on the NURBS patch using the Newton-Rapshon iteration method
    bool hasResidualConverged;
    bool hasConverged = thePatch->computePointProjectionOnPatch(u, v, projectedP,
                                                                hasResidualConverged, newtonRaphson.maxNumOfIterations, newtonRaphson.tolerance);
    double distance = MathLibrary::computePointDistance(P, projectedP);
    if(hasConverged &&  distance < projectionProperties.maxProjectionDistance) {
        /// Perform some validity checks to validate the projected point
        if(distance > minProjectionDistance + projectionProperties.maxDistanceForProjectedPointsOnDifferentPatches) {
            return false;
        }
        if(!minProjectionPoint.empty() &&
                MathLibrary::computePointDistance(projectedP, &minProjectionPoint[0]) > projectionProperties.maxDistanceForProjectedPointsOnDifferentPatches &&
                distance > minProjectionDistance) {
            return false;
        }
        if(distance < minProjectionDistance - projectionProperties.maxDistanceForProjectedPointsOnDifferentPatches
                || MathLibrary::computePointDistance(projectedP, &minProjectionPoint[0]) > projectionProperties.maxDistanceForProjectedPointsOnDifferentPatches) {
            projectedCoords[nodeIndex].clear();
        }
        /// Store result
        vector<double> uv(2);
        uv[0] = u;
        uv[1] = v;
        projectedCoords[nodeIndex].insert(make_pair(patchIndex, uv));
        minProjectionDistance = distance;
        minProjectionPoint = vector<double>(projectedP, projectedP + 3);
        return true;
    }
    return false;
}

bool IGAMortarMapper::forceProjectPointOnPatch(const int patchIndex, const int nodeIndex, double& minProjectionDistance, vector<double>& minProjectionPoint) {
    IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(patchIndex);
    /// Get the Cartesian coordinates of the node in the FE side
    double P[3], projectedP[3];
    projectedP[0] = P[0] = meshFE->nodes[nodeIndex * 3 + 0];
    projectedP[1] = P[1] = meshFE->nodes[nodeIndex * 3 + 1];
    projectedP[2] = P[2] = meshFE->nodes[nodeIndex * 3 + 2];
    /// Get an initial guess for the parametric location of the projected node of the FE side on the NURBS patch
    double u = 0;
    double v = 0;
    for(vector<int>::const_iterator it=meshFENodeToElementTable[nodeIndex].begin();it!=meshFENodeToElementTable[nodeIndex].end();it++) {
        const int numNodesPerElem = meshFE->numNodesPerElem[*it];
        for(int i = 0; i< numNodesPerElem; i++) {
            if(projectedCoords[meshFEDirectElemTable[*it][i]].find(patchIndex) != projectedCoords[meshFEDirectElemTable[*it][i]].end()) {
                u=projectedCoords[meshFEDirectElemTable[*it][i]][patchIndex][0];
                v=projectedCoords[meshFEDirectElemTable[*it][i]][patchIndex][1];
            }
        }
    }
    /// Compute approximate of parametric position based on brute sampling
    thePatch->findInitialGuess4PointProjection(u,v,P,200,200);
    double uv[2] = {u, v};
    thePatch->computeCartesianCoordinates(projectedP, uv);
    double distance = MathLibrary::computePointDistance(P, projectedP);
    /// Perform some validity checks
    if(distance > minProjectionDistance + projectionProperties.maxDistanceForProjectedPointsOnDifferentPatches) {
        return false;
    }
    if(distance < minProjectionDistance - projectionProperties.maxDistanceForProjectedPointsOnDifferentPatches) {
        projectedCoords[nodeIndex].clear();
    }
    /// Store result
    vector<double> coordTmp(2);
    coordTmp[0] = u;
    coordTmp[1] = v;
    projectedCoords[nodeIndex].insert(make_pair(patchIndex, coordTmp));
    minProjectionDistance = distance;
    minProjectionPoint = vector<double>(projectedP, projectedP + 3);
    return true;
}

void IGAMortarMapper::computeCouplingMatrices() {
    /*
     * Computes the coupling matrices CNR and CNN.
     * Loop over all the elements in the FE side
     * ->
     * 1. Find whether the projected FE element is located on one patch or split
     *
     * 2. Compute the coupling matrices
     * ->
     * 2i. Loop over patches where element can be projected entirely on one patch
     * 2ii. Loop over patches where element is split
     * <-
     */
    // Time stamps
    time_t timeStart, timeEnd;

    /// List of integrated element
    set<int> elementIntegrated;

    /// The vertices of the canonical polygons
    double parentTriangle[6] = { 0, 0, 1, 0, 0, 1 };
    double parentQuadriliteral[8] = { -1, -1, 1, -1, 1, 1, -1, 1 };

    int elementStringLength;
    {
        stringstream ss;
        ss << meshFE->numElems;
        elementStringLength = ss.str().length();
    }

    INFO_OUT() << "Computing coupling matrices starting ..." << endl;
    time(&timeStart);
    /// Loop over all the elements in the FE side
    for (int elemIndex = 0; elemIndex < meshFE->numElems; elemIndex++) {
        DEBUG_OUT()<< setfill ('#') << setw(18+elementStringLength) << "#" << endl;
        DEBUG_OUT()<< setfill (' ') << "### ELEMENT ["<< setw(elementStringLength) << elemIndex << "] ###"<<endl;
        DEBUG_OUT()<< setfill ('#') << setw(18+elementStringLength) << "#" << setfill (' ')<< endl;
        // Get the number of shape functions. Depending on number of nodes in the current element
        int numNodesElementFE = meshFE->numNodesPerElem[elemIndex];
        /// Find whether the projected FE element is located on one patch
        set<int> patchWithFullElt;
        set<int> patchWithSplitElt;
        getPatchesIndexElementIsOn(elemIndex, patchWithFullElt, patchWithSplitElt);
        DEBUG_OUT()<<"Element FULLY projected on \t" << patchWithFullElt.size() << " patch." << endl;
        DEBUG_OUT()<<"Element PARTLY projected on \t" << patchWithSplitElt.size() << " patch." << endl;
        /////////////////////////////////////
        /// Compute the coupling matrices ///
        /////////////////////////////////////
        /// 1. If the current element can be projected on one patch
        for (set<int>::iterator it = patchWithFullElt.begin();
             it != patchWithFullElt.end(); ++it) {
            int patchIndex=*it;
            /// Get the projected coordinates for the current element
            Polygon2D polygonUV;
            bool isProjectedOnPatchBoundary=true;
            /// 1.1 Get initial polygon from projection
            // For every point of polygon
            buildFullParametricElement(elemIndex, numNodesElementFE, patchIndex, polygonUV);
            ClipperAdapter::cleanPolygon(polygonUV);
            bool isIntegrated = computeLocalCouplingMatrix(elemIndex, patchIndex, polygonUV);
            if(isIntegrated) {
                elementIntegrated.insert(elemIndex);
                projectedPolygons[elemIndex][patchIndex]=polygonUV;
            }
        }
        /// 2. If the current element is split in more than one patches
        // Loop over all the patches in the IGA Mesh having a part of the FE element projected inside
        for (set<int>::iterator it = patchWithSplitElt.begin(); it != patchWithSplitElt.end(); it++) {
            int patchIndex=*it;
            IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(patchIndex);
            // Stores points of the polygon clipped by the nurbs patch
            Polygon2D polygonUV;
            buildBoundaryParametricElement(elemIndex, numNodesElementFE, patchIndex, polygonUV);
            ClipperAdapter::cleanPolygon(polygonUV);
            bool isIntegrated = computeLocalCouplingMatrix(elemIndex, patchIndex, polygonUV);
            if(isIntegrated) {
                elementIntegrated.insert(elemIndex);
                projectedPolygons[elemIndex][patchIndex]=polygonUV;
            }
        } // end of loop over set of split patch
    } // end of loop over all the element
    time(&timeEnd);
    INFO_OUT() << "Computing coupling matrices done! It took " << difftime(timeEnd, timeStart) << " seconds." << endl;
    if(elementIntegrated.size() != meshFE->numElems) {
        WARNING_OUT()<<"Number of FE mesh integrated is "<<elementIntegrated.size()<<" over "<<meshFE->numElems<<endl;
        for(int i = 0; i < meshFE->numElems; i++) {
            if(!elementIntegrated.count(i))
                WARNING_OUT()<<"Missing element number "<< i <<endl;
        }
        WARNING_BLOCK_OUT("IGAMortarMapper","ComputeCouplingMatrices","Not all element in FE mesh integrated ! Coupling matrices invalid");
    }
}

void IGAMortarMapper::computeIGAPatchWeakContinuityConditionMatrices() {

    INFO_OUT() << "Application of weak patch continuity conditions started" << endl;

    double alphaPrim = IgaPatchCoupling.dispPenalty;
    double alphaSec = IgaPatchCoupling.rotPenalty;
    INFO_OUT()<<"Manual patch coupling penalties: alphaPrim = "<<alphaPrim<<" alphaSec = "<<alphaSec<<endl;

    std::vector<WeakIGAPatchContinuityCondition*> weakIGAPatchContinuityConditions = meshIGA->getWeakIGAPatchContinuityConditions();

    // Define tolerances
    const double tolAngle = 1e-1;

    // Initialize constant array sizes
    const int noCoord = 3;
    const int numParametricDim = 2;

    // Initialize varying array sizes
    int indexMaster;
    int indexSlave;
    double uGPMaster;
    double vGPMaster;
    double uGPSlave;
    double vGPSlave;
    double condTangentTrCurveVct;
    double condNormalTrCurveVct;
    double surfaceNormalVctMaster[noCoord];
    double surfaceNormalVctSlave[noCoord];
    double tangentTrCurveVctMaster[noCoord];
    double tangentTrCurveVctSlave[noCoord];
    double normalTrCurveVctMaster[noCoord];
    double normalTrCurveVctSlave[noCoord];
    double surfNormalVctAndDervsMaster[3*noCoord];
    double surfNormalVctAndDervsSlave[3*noCoord];
    double covariantMetricTensorMaster[4];
    double covariantMetricTensorSlave[4];
    double contravariantBaseVctsMaster[6];
    double contravariantBaseVctsSlave[6];
    int uKnotSpanMaster;
    int vKnotSpanMaster;
    int uKnotSpanSlave;
    int vKnotSpanSlave;
    int noLocalBasisFctsMaster;
    int noLocalBasisFctsSlave;
    int noDOFsLocMaster;
    int noDOFsLocSlave;
    int noGPsOnContCond;
    int derivDegreeBasis = 2;
    int derivDegreeBaseVec = derivDegreeBasis - 1;
    int noBaseVec = 2;
    int indexBasis;
    int indexBasisDerivU;
    int indexBasisDerivV;
    int indexCPI;
    int indexCPJ;
    int indexCP;
    int indexDOFLocI;
    int indexDOFLocJ;
    int indexDOFI;
    int indexDOFJ;
    int counter;
    int factorTangent = 1;
    int factorNormal = 1;
    bool isLinearSystemConvergent;

    // Initialize pointers
    double* trCurveMasterGPs;
    double* trCurveSlaveGPs;
    double* trCurveGPWeights;
    double* trCurveMasterGPTangents;
    double* trCurveSlaveGPTangents;
    double* trCurveGPJacobianProducts;
    IGAPatchSurface* patchMaster;
    IGAPatchSurface* patchSlave;

    // Loop over all the conditions for the application of weak continuity across patch interfaces
    for (int iWCC = 0; iWCC < weakIGAPatchContinuityConditions.size(); iWCC++){
        // Get the index of the master and slave patches
        indexMaster = weakIGAPatchContinuityConditions[iWCC]->getMasterPatchIndex();
        indexSlave = weakIGAPatchContinuityConditions[iWCC]->getSlavePatchIndex();

        // Get the number of Gauss Points for the given condition
        noGPsOnContCond = weakIGAPatchContinuityConditions[iWCC]->getTrCurveNumGP();

        // Get the parametric coordinates of the Gauss Points
        trCurveMasterGPs = weakIGAPatchContinuityConditions[iWCC]->getTrCurveMasterGPs();
        trCurveSlaveGPs = weakIGAPatchContinuityConditions[iWCC]->getTrCurveSlaveGPs();

        // Get the corresponding Gauss weights
        trCurveGPWeights = weakIGAPatchContinuityConditions[iWCC]->getTrCurveGPWeights();

        // Get the tangent vectors at the trimming curve of the given condition in the Cartesian space
        trCurveMasterGPTangents = weakIGAPatchContinuityConditions[iWCC]->getTrCurveMasterGPTangents();
        trCurveSlaveGPTangents = weakIGAPatchContinuityConditions[iWCC]->getTrCurveSlaveGPTangents();

        // Get the product of the Jacobian transformations
        trCurveGPJacobianProducts = weakIGAPatchContinuityConditions[iWCC]->getTrCurveGPJacobianProducts();

        // Get the master and the slave patch
        patchMaster = meshIGA->getSurfacePatch(indexMaster);
        patchSlave = meshIGA->getSurfacePatch(indexSlave);

        // Get the polynomial orders of the master and the slave patch
        int pMaster = patchMaster->getIGABasis()->getUBSplineBasis1D()->getPolynomialDegree();
        int qMaster = patchMaster->getIGABasis()->getVBSplineBasis1D()->getPolynomialDegree();
        int pSlave = patchSlave->getIGABasis()->getUBSplineBasis1D()->getPolynomialDegree();
        int qSlave = patchSlave->getIGABasis()->getVBSplineBasis1D()->getPolynomialDegree();

        // get the number of local basis functions for master and slave patch
        noLocalBasisFctsMaster = (pMaster+1)*(qMaster+1);
        noLocalBasisFctsSlave = (pSlave+1)*(qSlave+1);

        // get the number of the local DOFs for the master and slave patch
        noDOFsLocMaster = noCoord*noLocalBasisFctsMaster;
        noDOFsLocSlave = noCoord*noLocalBasisFctsSlave;

        // Initialize pointers
        double* basisFctsAndDerivsMaster = new double[(derivDegreeBasis + 1) * (derivDegreeBasis + 2)
                * noLocalBasisFctsMaster / 2];
        double* basisFctsAndDerivsSlave = new double[(derivDegreeBasis + 1) * (derivDegreeBasis + 2)
                * noLocalBasisFctsSlave / 2];
        double* baseVctsAndDerivsMaster = new double[(derivDegreeBaseVec + 1)
                * (derivDegreeBaseVec + 2) * noCoord * noBaseVec / 2];
        double* baseVctsAndDerivsSlave = new double[(derivDegreeBaseVec + 1)
                * (derivDegreeBaseVec + 2) * noCoord * noBaseVec / 2];
        double* BDisplacementsGCMaster = new double[noCoord*noDOFsLocMaster];
        double* BDisplacementsGCSlave = new double[noCoord*noDOFsLocSlave];
        double* BdDisplacementsdUGCMaster = new double[noCoord*noDOFsLocMaster];
        double* BdDisplacementsdVGCMaster = new double[noCoord*noDOFsLocMaster];
        double* BdDisplacementsdUGCSlave = new double[noCoord*noDOFsLocMaster];
        double* BdDisplacementsdVGCSlave = new double[noCoord*noDOFsLocMaster];

        // Loop over all the Gauss Points of the given condition
        for(int iGP = 0; iGP < noGPsOnContCond; iGP++){
            // Get the parametric coordinates of the Gauss Point on the master patch
            uGPMaster = trCurveMasterGPs[2*iGP];
            vGPMaster = trCurveMasterGPs[2*iGP + 1];
//            cout << "uGPMaster = " << uGPMaster << endl;
//            cout << "vGPMaster = " << vGPMaster << endl;

            // Get the parametric coordinates of the Gauss Point on the slave patch
            uGPSlave = trCurveSlaveGPs[2*iGP];
            vGPSlave = trCurveSlaveGPs[2*iGP + 1];
//            cout << "uGPSlave = " << uGPSlave << endl;
//            cout << "vGPSlave = " << vGPSlave << endl;

            // Find the knot span indices on the master patch
            uKnotSpanMaster = patchMaster->getIGABasis()->getUBSplineBasis1D()->findKnotSpan(uGPMaster);
            vKnotSpanMaster = patchMaster->getIGABasis()->getVBSplineBasis1D()->findKnotSpan(vGPMaster);

            // Find the knot span indices on the master patch
            uKnotSpanSlave = patchSlave->getIGABasis()->getUBSplineBasis1D()->findKnotSpan(uGPSlave);
            vKnotSpanSlave = patchSlave->getIGABasis()->getVBSplineBasis1D()->findKnotSpan(vGPSlave);

            // Compute the basis functions and their derivatives on the master patch
            patchMaster->getIGABasis()->computeLocalBasisFunctionsAndDerivatives(basisFctsAndDerivsMaster, derivDegreeBasis, uGPMaster,
                                                                                 uKnotSpanMaster, vGPMaster, vKnotSpanMaster);

            // Compute the basis functions and their derivatives on the slave patch
            patchSlave->getIGABasis()->computeLocalBasisFunctionsAndDerivatives(basisFctsAndDerivsSlave, derivDegreeBasis, uGPSlave,
                                                                                 uKnotSpanSlave, vGPSlave, vKnotSpanSlave);

//            for(int iTest=0;iTest<(pSlave+1)*(qSlave+1);iTest++){
//                int indexBasis = patchSlave->getIGABasis()->indexDerivativeBasisFunction(derivDegreeBasis,0,0,iTest);
//                INFO_OUT()<<"RSlave[i] = "<< basisFctsAndDerivsSlave[indexBasis] <<std::endl;
//            }

            // Compute the base vectors and their derivatives on the master patch
            patchMaster->computeBaseVectorsAndDerivatives(baseVctsAndDerivsMaster, basisFctsAndDerivsMaster, derivDegreeBaseVec,
                                                          uKnotSpanMaster, vKnotSpanMaster);

            // Compute the base vectors and their derivatives on the slave patch
            patchSlave->computeBaseVectorsAndDerivatives(baseVctsAndDerivsSlave, basisFctsAndDerivsSlave, derivDegreeBaseVec,
                                                          uKnotSpanSlave, vKnotSpanSlave);

            // Compute the derivatives of the surface normal base vector for the master patch
            patchMaster->computeSurfaceNormalVectorAndDerivatives(surfNormalVctAndDervsMaster,baseVctsAndDerivsMaster,derivDegreeBaseVec);
//            std::cout << "surfNormalVctAndDervsMaster = ( " << std::endl;
//            for(int i = 0; i < 9; i++){
//                std::cout << surfNormalVctAndDervsMaster[i] << " , ";
//            }
//            std::cout << std::endl;

            // Compute the derivatives of the surface normal base vector for the master patch
            patchSlave->computeSurfaceNormalVectorAndDerivatives(surfNormalVctAndDervsSlave,baseVctsAndDerivsSlave,derivDegreeBaseVec);
//            std::cout << "surfNormalVctAndDervsSlave = ( " << std::endl;
//            for(int i = 0; i < 9; i++){
//                std::cout << surfNormalVctAndDervsSlave[i] << " , ";
//            }
//            std::cout << std::endl;
//            exit(-1);

            // Compute the normal and the tangent to the trimming curves vectors for the master patch
//            std::cout << "tangentVctMaster = ( " << std::endl;
            for(int i = 0; i < noCoord; i++){
                tangentTrCurveVctMaster[i] = trCurveMasterGPTangents[3*iGP + i];
                surfaceNormalVctMaster[i] = surfNormalVctAndDervsMaster[i];
//                std::cout << tangentTrCurveVctMaster[i] << " , ";
            }
            std::cout << " )" << endl;
            EMPIRE::MathLibrary::computeVectorCrossProduct(surfaceNormalVctMaster,tangentTrCurveVctMaster,normalTrCurveVctMaster);
//            std::cout << "normalVctMaster = ( " << std::endl;
//            for(int i = 0; i < noCoord; i++){
//                std::cout << normalTrCurveVctMaster[i] << " , ";
//            }
//            std::cout << " )" << endl;

            // Compute the normal and the tangent to the trimming curves vectors for the slave patch
//            std::cout << "tangentVctSlave = ( " << std::endl;
            for(int i = 0; i < noCoord; i++){
                tangentTrCurveVctSlave[i] = trCurveSlaveGPTangents[3*iGP + i];
                surfaceNormalVctSlave[i] = surfNormalVctAndDervsSlave[i];
//                std::cout << tangentTrCurveVctSlave[i] << " , ";
            }
//            std::cout << " )" << endl;
            EMPIRE::MathLibrary::computeVectorCrossProduct(surfaceNormalVctSlave,tangentTrCurveVctSlave,normalTrCurveVctSlave);
//            std::cout << "normalVctSlave = ( " << std::endl;
//            for(int i = 0; i < noCoord; i++){
//                std::cout << normalTrCurveVctSlave[i] << " , ";
//            }
//            std::cout << " )" << endl;

            // Determine the alignment of the tangent and the normal vectors from both patches at their common interface
            condTangentTrCurveVct = EMPIRE::MathLibrary::computeDenseDotProduct(noCoord,tangentTrCurveVctMaster,tangentTrCurveVctSlave);
            assert(abs(condTangentTrCurveVct) > tolAngle);
            if(condTangentTrCurveVct > tolAngle){
                factorTangent = -1;
            }
            condNormalTrCurveVct = EMPIRE::MathLibrary::computeDenseDotProduct(noCoord,normalTrCurveVctMaster,normalTrCurveVctSlave);
            assert(abs(condNormalTrCurveVct) > tolAngle);
            if(condNormalTrCurveVct > tolAngle){
                factorNormal = -1;
            }

            // Compute the covariant metric tensor of the master patch
            patchMaster->computeCovariantMetricTensor(covariantMetricTensorMaster,baseVctsAndDerivsMaster,derivDegreeBaseVec);
//            std::cout << "covariantMetricTensorMaster = ( " << std::endl;
//            for(int i = 0; i < 4; i++){
//                std::cout << covariantMetricTensorMaster[i] << " , ";
//            }
//            std::cout << " )" << endl;

            // Compute the covariant metric tensor of the slave patch
            patchSlave->computeCovariantMetricTensor(covariantMetricTensorSlave,baseVctsAndDerivsSlave,derivDegreeBaseVec);
//            std::cout << "covariantMetricTensorSlave = ( " << std::endl;
//            for(int i = 0; i < 4; i++){
//                std::cout << covariantMetricTensorSlave[i] << " , ";
//            }
//            std::cout << " )" << endl;

            // Compute the contravariant base vectors
            // isLinearSystemConvergent = EMPIRE::MathLibrary::solve2x2LinearSystem(dR, R, EMPIRE::MathLibrary::EPS );

            // Compute the contravariant base vectors of the master patch
            patchMaster->computeContravariantBaseVectors(contravariantBaseVctsMaster, covariantMetricTensorMaster, baseVctsAndDerivsMaster, derivDegreeBaseVec);
//            std::cout << "contraBaseVctUMaster = ( ";
//            for(int i = 0; i < 3; i++){
//                std::cout << contravariantBaseVctsMaster[i] << " , ";
//            }
//            std::cout << " )" << endl;
//            std::cout << "contraBaseVctVMaster = ( ";
//            for(int i = 0; i < 3; i++){
//                std::cout << contravariantBaseVctsMaster[noCoord + i] << " , ";
//            }
//            std::cout << " )" << endl;

            // Compute the contravariant base vectors of the slave patch
            patchSlave->computeContravariantBaseVectors(contravariantBaseVctsSlave, covariantMetricTensorSlave, baseVctsAndDerivsSlave, derivDegreeBaseVec);
//            std::cout << "contraBaseVctUSlave = ( ";
//            for(int i = 0; i < 3; i++){
//                std::cout << contravariantBaseVctsSlave[i] << " , ";
//            }
//            std::cout << " )" << endl;
//            std::cout << "contraBaseVctVSlave = ( ";
//            for(int i = 0; i < 3; i++){
//                std::cout << contravariantBaseVctsSlave[noCoord + i] << " , ";
//            }
//            std::cout << " )" << endl;





            // Andreas :: DEBUG
//            double G1Slave[3];
//            cout << "G1Slave = (";
//            for(int iBV = 0; iBV < 3; iBV++){
//                int indexBaseVct;
//                int i = 0;
//                int j = 0;
//                indexBaseVct = patchSlave->indexDerivativeBaseVector(derivDegreeBaseVec, i, j, iBV, 0);
//                G1Slave[iBV] = baseVctsAndDerivsSlave[indexBaseVct];
//                cout << G1Slave[iBV] << ", ";
//            }
//            cout << ")" << endl;

//            double G1SlaveKommaXi[3];
//            cout << "G1SlaveKommaXi = (";
//            for(int iBV = 0; iBV < 3; iBV++){
//                int indexBaseVct;
//                int i = 1;
//                int j = 0;
//                indexBaseVct = patchSlave->indexDerivativeBaseVector(derivDegreeBaseVec, i, j, iBV, 0);
//                G1SlaveKommaXi[iBV] = baseVctsAndDerivsSlave[indexBaseVct];
//                cout << G1SlaveKommaXi[iBV] << ", ";
//            }
//            cout << ")" << endl;

//            double G1SlaveKommaEta[3];
//            cout << "G1SlaveKommaEta = (";
//            for(int iBV = 0; iBV < 3; iBV++){
//                int indexBaseVct;
//                int i = 0;
//                int j = 1;
//                indexBaseVct = patchSlave->indexDerivativeBaseVector(derivDegreeBaseVec, i, j, iBV, 0);
//                G1SlaveKommaEta[iBV] = baseVctsAndDerivsSlave[indexBaseVct];
//                cout << G1SlaveKommaEta[iBV] << ", ";
//            }
//            cout << ")" << endl;

//            double G2Slave[3];
//            cout << "G2Slave = (";
//            for(int iBV = 0; iBV < 3; iBV++){
//                int indexBaseVct;
//                int i = 0;
//                int j = 0;
//                indexBaseVct = patchSlave->indexDerivativeBaseVector(derivDegreeBaseVec, i, j, iBV, 1);
//                G2Slave[iBV] = baseVctsAndDerivsSlave[indexBaseVct];
//                cout << G2Slave[iBV] << ", ";
//            }
//            cout << ")" << endl;

//            double G2SlaveKommaXi[3];
//            cout << "G2SlaveKommaXi = (";
//            for(int iBV = 0; iBV < 3; iBV++){
//                int indexBaseVct;
//                int i = 1;
//                int j = 0;
//                indexBaseVct = patchSlave->indexDerivativeBaseVector(derivDegreeBaseVec, i, j, iBV, 1);
//                G2SlaveKommaXi[iBV] = baseVctsAndDerivsSlave[indexBaseVct];
//                cout << G2SlaveKommaXi[iBV] << ", ";
//            }
//            cout << ")" << endl;

//            double G2SlaveKommaEta[3];
//            cout << "G2SlaveKommaEta = (";
//            for(int iBV = 0; iBV < 3; iBV++){
//                int indexBaseVct;
//                int i = 0;
//                int j = 1;
//                indexBaseVct = patchSlave->indexDerivativeBaseVector(derivDegreeBaseVec, i, j, iBV, 1);
//                G2SlaveKommaEta[iBV] = baseVctsAndDerivsSlave[indexBaseVct];
//                cout << G2SlaveKommaEta[iBV] << ", ";
//            }
//            cout << ")" << endl;
//            exit(-1);

            // Initialize the B-operator matrix for the displacement field of the master patch
            for(int iBF = 0; iBF < noCoord*noDOFsLocMaster; iBF++){
                BDisplacementsGCMaster[iBF] = 0.0;
                BdDisplacementsdUGCMaster[iBF] = 0.0;
                BdDisplacementsdVGCMaster[iBF] = 0.0;
            }

            // Compute the B-operator matrix for the displacement field of the master patch
            for(int iBF = 0; iBF < noLocalBasisFctsMaster; iBF++){
                indexBasis = patchMaster->getIGABasis()->indexDerivativeBasisFunction(derivDegreeBasis,0,0,iBF);
                BDisplacementsGCMaster[0*noDOFsLocMaster + 3*iBF + 0] = basisFctsAndDerivsMaster[indexBasis];
                BDisplacementsGCMaster[1*noDOFsLocMaster + 3*iBF + 1] = basisFctsAndDerivsMaster[indexBasis];
                BDisplacementsGCMaster[2*noDOFsLocMaster + 3*iBF + 2] = basisFctsAndDerivsMaster[indexBasis];
                indexBasisDerivU = patchMaster->getIGABasis()->indexDerivativeBasisFunction(derivDegreeBasis,1,0,iBF);
                BdDisplacementsdUGCMaster[0*noDOFsLocMaster + 3*iBF + 0] = basisFctsAndDerivsMaster[indexBasisDerivU];
                BdDisplacementsdUGCMaster[1*noDOFsLocMaster + 3*iBF + 1] = basisFctsAndDerivsMaster[indexBasisDerivU];
                BdDisplacementsdUGCMaster[2*noDOFsLocMaster + 3*iBF + 2] = basisFctsAndDerivsMaster[indexBasisDerivU];
                indexBasisDerivV = patchMaster->getIGABasis()->indexDerivativeBasisFunction(derivDegreeBasis,0,1,iBF);
                BdDisplacementsdVGCMaster[0*noDOFsLocMaster + 3*iBF + 0] = basisFctsAndDerivsMaster[indexBasisDerivV];
                BdDisplacementsdVGCMaster[1*noDOFsLocMaster + 3*iBF + 1] = basisFctsAndDerivsMaster[indexBasisDerivV];
                BdDisplacementsdVGCMaster[2*noDOFsLocMaster + 3*iBF + 2] = basisFctsAndDerivsMaster[indexBasisDerivV];
            }
//            EMPIRE::MathLibrary::printGeneralMatrix(BdDisplacementsdUGCMaster,noCoord,noDOFsLocMaster);
//            EMPIRE::MathLibrary::printGeneralMatrix(BdDisplacementsdVGCMaster,noCoord,noDOFsLocMaster);

            // Initialize the B-operator matrix for the displacement field of the slave patch
            for(int iBF = 0; iBF < noCoord*noDOFsLocSlave; iBF++){
                BDisplacementsGCSlave[iBF] = 0.0;
                BdDisplacementsdUGCSlave[iBF] = 0.0;
                BdDisplacementsdVGCSlave[iBF] = 0.0;
            }

            // Compute the B-operator matrix for the displacement field of the slave patch
            for(int iBF = 0; iBF < noLocalBasisFctsSlave; iBF++){
                indexBasis = patchSlave->getIGABasis()->indexDerivativeBasisFunction(derivDegreeBasis,0,0,iBF);
                BDisplacementsGCSlave[0*noDOFsLocSlave + 3*iBF + 0] = (-basisFctsAndDerivsSlave[indexBasis]);
                BDisplacementsGCSlave[1*noDOFsLocSlave + 3*iBF + 1] = (-basisFctsAndDerivsSlave[indexBasis]);
                BDisplacementsGCSlave[2*noDOFsLocSlave + 3*iBF + 2] = (-basisFctsAndDerivsSlave[indexBasis]);
                indexBasisDerivU = patchSlave->getIGABasis()->indexDerivativeBasisFunction(derivDegreeBasis,1,0,iBF);
                BdDisplacementsdUGCSlave[0*noDOFsLocSlave + 3*iBF + 0] = basisFctsAndDerivsSlave[indexBasisDerivU];
                BdDisplacementsdUGCSlave[1*noDOFsLocSlave + 3*iBF + 1] = basisFctsAndDerivsSlave[indexBasisDerivU];
                BdDisplacementsdUGCSlave[2*noDOFsLocSlave + 3*iBF + 2] = basisFctsAndDerivsSlave[indexBasisDerivU];
                indexBasisDerivV = patchSlave->getIGABasis()->indexDerivativeBasisFunction(derivDegreeBasis,0,1,iBF);
                BdDisplacementsdVGCSlave[0*noDOFsLocSlave + 3*iBF + 0] = basisFctsAndDerivsSlave[indexBasisDerivV];
                BdDisplacementsdVGCSlave[1*noDOFsLocSlave + 3*iBF + 1] = basisFctsAndDerivsSlave[indexBasisDerivV];
                BdDisplacementsdVGCSlave[2*noDOFsLocSlave + 3*iBF + 2] = basisFctsAndDerivsSlave[indexBasisDerivV];
            }

            // calculate elementLength on GP (mapping from unit space to physical times the gpWeight)
            double elementLengthOnGP = trCurveGPJacobianProducts[iGP]*trCurveGPWeights[iGP];

            // Initialize dual product matrices
            double* KPenaltyMaster = new double[noDOFsLocMaster*noDOFsLocMaster];
            double* KPenaltySlave = new double[noDOFsLocSlave*noDOFsLocSlave];
            double* CPenalty = new double[noDOFsLocMaster*noDOFsLocSlave];
            for(int i = 0; i < noDOFsLocMaster*noDOFsLocMaster; i++)
                KPenaltyMaster[i] = 0.0;
            for(int i = 0; i < noDOFsLocSlave*noDOFsLocSlave; i++)
                KPenaltySlave[i] = 0.0;
            for(int i = 0; i < noDOFsLocMaster*noDOFsLocSlave; i++)
                CPenalty[i] = 0.0;

            // Compute the dual product matrices
            EMPIRE::MathLibrary::computeTransposeMatrixProduct(noCoord,noDOFsLocMaster,noDOFsLocMaster,BDisplacementsGCMaster,BDisplacementsGCMaster,KPenaltyMaster);
            EMPIRE::MathLibrary::computeTransposeMatrixProduct(noCoord,noDOFsLocSlave,noDOFsLocSlave,BDisplacementsGCSlave,BDisplacementsGCSlave,KPenaltySlave);
            EMPIRE::MathLibrary::computeTransposeMatrixProduct(noCoord,noDOFsLocMaster,noDOFsLocSlave,BDisplacementsGCMaster,BDisplacementsGCSlave,CPenalty);

            // Compute the element index tables for the master and slave patch
            int CPIndexMaster[noLocalBasisFctsMaster];
            int CPIndexSlave[noLocalBasisFctsSlave];
            patchMaster->getIGABasis()->getBasisFunctionsIndex(uKnotSpanMaster, vKnotSpanMaster, CPIndexMaster);
            patchSlave->getIGABasis()->getBasisFunctionsIndex(uKnotSpanSlave, vKnotSpanSlave, CPIndexSlave);

            // Compute the element freedom tables for the master and the slave patch
            int EFTMaster[noDOFsLocMaster];
            counter = 0;
            for (int i = 0; i < noLocalBasisFctsMaster ; i++){
                indexCP = patchMaster->getControlPointNet()[CPIndexMaster[i]]->getDofIndex();
                for (int j = 0; j < noCoord; j++){
                    EFTMaster[counter] = noCoord*indexCP + j;
                    counter++;
                }
            }

            counter = 0;
            int EFTSlave[noDOFsLocSlave];
            for (int i = 0; i < noLocalBasisFctsSlave ; i++){
                indexCP = patchSlave->getControlPointNet()[CPIndexSlave[i]]->getDofIndex();
                for (int j = 0; j < noCoord; j++){
                    EFTSlave[counter] = noCoord*indexCP + j;
                    counter++;
                }
            }

            // Assemble KPenaltyMaster to the global coupling matrix CNN
            for(int i = 0; i < noDOFsLocMaster; i++){
                for(int j = 0; j < noDOFsLocMaster; j++){
                    couplingMatrices->addCNN_expandedValue(EFTMaster[i], EFTMaster[j], alphaPrim*KPenaltyMaster[i*noDOFsLocMaster + j]*elementLengthOnGP);
                }
            }

            // Assemble KPenaltySlave to the global coupling matrix CNN
            for(int i = 0; i < noDOFsLocSlave; i++){
                for(int j = 0; j < noDOFsLocSlave; j++) {
                    couplingMatrices->addCNN_expandedValue(EFTSlave[i], EFTSlave[j], alphaPrim*KPenaltySlave[i*noDOFsLocSlave + j]*elementLengthOnGP);
                }
            }

            // Assemble CPenaltyMaster to the global coupling matrix CNN
            for(int i = 0; i < noDOFsLocMaster; i++){
                for(int j = 0; j < noDOFsLocSlave; j++){
                    couplingMatrices->addCNN_expandedValue(EFTMaster[i], EFTSlave[j], alphaPrim*CPenalty[i*noDOFsLocSlave + j]*elementLengthOnGP);
                    couplingMatrices->addCNN_expandedValue(EFTSlave[j], EFTMaster[i], alphaPrim*CPenalty[i*noDOFsLocSlave + j]*elementLengthOnGP);
                }
            }

            // Delete pointers
            delete[] KPenaltyMaster;
            delete[] KPenaltySlave;
            delete[] CPenalty;
        } // End of Gauss Point loop

        // Delete pointers
        delete[] basisFctsAndDerivsMaster;
        delete[] basisFctsAndDerivsSlave;
        delete[] baseVctsAndDerivsMaster;
        delete[] baseVctsAndDerivsSlave;
        delete[] BDisplacementsGCMaster;
        delete[] BdDisplacementsdUGCMaster;
        delete[] BdDisplacementsdVGCMaster;
        delete[] BDisplacementsGCSlave;
        delete[] BdDisplacementsdUGCSlave;
        delete[] BdDisplacementsdVGCSlave;
    } // End of weak continuity condition loop

    INFO_OUT()<<"Application of weak patch continuity conditions finished"<<std::endl;
}

void IGAMortarMapper::computePenaltyFactorsForPatchCoupling(double& alphaPrim, double& alphaSec, IGAPatchSurface* masterPatch,
                                                            IGAPatchSurface* slavePatch, double* gausspoints_master, double* gausspoints_slave, double* gausspoints_weight,
                                                            double* mappings, int numElemsPerBRep, int numGPsPerElem) {

    std::vector<double> allElementLengths;

    int uNoKnots_master = masterPatch->getIGABasis()->getUBSplineBasis1D()->getNoKnots();
    int vNoKnots_master = masterPatch->getIGABasis()->getVBSplineBasis1D()->getNoKnots();
    int p_master = masterPatch->getIGABasis()->getUBSplineBasis1D()->getPolynomialDegree();
    int q_master = masterPatch->getIGABasis()->getVBSplineBasis1D()->getPolynomialDegree();

    // loop through all nonzero knotspans
    for(int uKnotCounter = p_master ; uKnotCounter < uNoKnots_master - p_master - 1 ; uKnotCounter++) {
        for(int vKnotCounter = q_master ; vKnotCounter < vNoKnots_master - q_master - 1 ; vKnotCounter++) {
            double elementLength_master = 0;

            // loop through all gausspoints of the current BReP
            for(int gpCounter = 0 ; gpCounter < numElemsPerBRep*numGPsPerElem ; gpCounter++) {
                int uKnotSpanGP_m = masterPatch->getIGABasis()->getUBSplineBasis1D()->findKnotSpan(gausspoints_master[2*gpCounter]);
                int vKnotSpanGP_m = masterPatch->getIGABasis()->getVBSplineBasis1D()->findKnotSpan(gausspoints_master[2*gpCounter+1]);
                if(uKnotCounter == uKnotSpanGP_m && vKnotCounter == vKnotSpanGP_m) {
                    elementLength_master += mappings[gpCounter] * gausspoints_weight[gpCounter];
                }
            }
            if(elementLength_master > 0) {
                allElementLengths.push_back(elementLength_master);
            }
        }
    }

    int uNoKnots_slave = slavePatch->getIGABasis()->getUBSplineBasis1D()->getNoKnots();
    int vNoKnots_slave = slavePatch->getIGABasis()->getVBSplineBasis1D()->getNoKnots();
    int p_slave = slavePatch->getIGABasis()->getUBSplineBasis1D()->getPolynomialDegree();
    int q_slave = slavePatch->getIGABasis()->getVBSplineBasis1D()->getPolynomialDegree();

    // loop through all nonzero knotspans
    for(int uKnotCounter = p_slave ; uKnotCounter < uNoKnots_slave - p_slave - 1 ; uKnotCounter++) {
        for(int vKnotCounter = q_slave ; vKnotCounter < vNoKnots_slave - q_slave - 1 ; vKnotCounter++) {
            double elementLength_slave = 0;

            // loop through all gausspoints of the current BReP
            for(int gpCounter = 0 ; gpCounter < numElemsPerBRep*numGPsPerElem ; gpCounter++) {
                int uKnotSpanGP_s = slavePatch->getIGABasis()->getUBSplineBasis1D()->findKnotSpan(gausspoints_slave[2*gpCounter]);
                int vKnotSpanGP_s = slavePatch->getIGABasis()->getVBSplineBasis1D()->findKnotSpan(gausspoints_slave[2*gpCounter+1]);
                if(uKnotCounter == uKnotSpanGP_s && vKnotCounter == vKnotSpanGP_s) {
                    elementLength_slave += mappings[gpCounter] * gausspoints_weight[gpCounter];
                }
            }
            if(elementLength_slave > 0) {
                allElementLengths.push_back(elementLength_slave);
            }
        }
    }

    // find the smallest element length
    double smallestElementLength = allElementLengths[0];
    for(int elementCounter = 1 ; elementCounter < allElementLengths.size() ; elementCounter++) {
        if(allElementLengths[elementCounter] < smallestElementLength)
            smallestElementLength = allElementLengths[elementCounter];
    }

    alphaPrim = 1/smallestElementLength;
    alphaSec = 1/(sqrt(smallestElementLength));
}

void IGAMortarMapper::getPatchesIndexElementIsOn(int elemIndex, set<int>& patchWithFullElt, set<int>& patchWithSplitElt) {
    // Initialize the flag whether the projected FE element is located on one patch
    bool isAllNodesOnPatch = true;
    // Initialize the flag whether the projected FE element is not located at all on the patch
    bool isAllNodesOut= true;
    // Loop over all the patches
    for (int patchCount = 0; patchCount < meshIGA->getSurfacePatches().size(); patchCount++) {
        isAllNodesOnPatch = true;
        isAllNodesOut= true;
        // Loop over all the nodes of the unclipped element
        for (int nodeCount = 0; nodeCount < meshFE->numNodesPerElem[elemIndex]; nodeCount++) {
            // Find the index of the node in the FE mesh
            int nodeIndex = meshFEDirectElemTable[elemIndex][nodeCount];
            // Find whether this index is in the projected nodes array
            bool isNodeOnPatch = projectedCoords[nodeIndex].find(patchCount)
                    != projectedCoords[nodeIndex].end();
            // Update flag
            if (!isNodeOnPatch) {
                isAllNodesOnPatch = false;
            } else {
                isAllNodesOut=false;
            }
        }
        // If all nodes are on the patch "patchCount", save this patch and go for next patch
        if (isAllNodesOnPatch) {
            patchWithFullElt.insert(patchCount);
            continue;
        }
        // If element is splitted for patch "patchCount", save this patch and go for next patch
        if(!isAllNodesOut) {
            patchWithSplitElt.insert(patchCount);
            continue;
        }
    }
}

void IGAMortarMapper::buildFullParametricElement(int elemCount, int numNodesElementFE, int patchIndex, Polygon2D& polygonUV) {
    // Just look into projectedCoords structure and build the polygon
    for (int nodeCount = 0; nodeCount < numNodesElementFE; nodeCount++) {
        int nodeIndex = meshFEDirectElemTable[elemCount][nodeCount];
        double u = projectedCoords[nodeIndex][patchIndex][0];
        double v = projectedCoords[nodeIndex][patchIndex][1];
        polygonUV.push_back(make_pair(u,v));
    }
}

void IGAMortarMapper::buildBoundaryParametricElement(int elemIndex, int numNodesElementFE, int patchIndex, Polygon2D& polygonUV) {
    IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(patchIndex);
    /// Split nodes from element into 2 subsets, node projected inside the NURBS patch, and node projected outside
    vector<int> insideNode, outsideNode;
    for(int nodeCount = 0; nodeCount < numNodesElementFE; nodeCount++) {
        int nodeIndex = meshFEDirectElemTable[elemIndex][nodeCount];
        /// Flag on whether the node is inside the current NURBS patch
        bool isNodeInsidePatch = projectedCoords[nodeIndex].find(patchIndex)
                != projectedCoords[nodeIndex].end();
        if(isNodeInsidePatch)
            insideNode.push_back(nodeIndex);
        else
            outsideNode.push_back(nodeIndex);
    }
    /// Tolerance validity projected point
    /// WARNING hard coded tolerance for valid
    double toleranceRatio = 1e-6;
    /// Process every node of the element
    for(int i = 0; i < numNodesElementFE; i++) {
        bool isProjectedOnPatchBoundary = true;
        double uIn, vIn;
        double u, v;
        double div = 0, dis = projectionProperties.maxProjectionDistance;

        /// Get the node indexes
        int nodeCount = (i + 0) % numNodesElementFE;
        int nodeCountPrev = (i + numNodesElementFE - 1) % numNodesElementFE;
        int nodeCountNext = (i + 1) % numNodesElementFE;
        int nodeIndex = meshFEDirectElemTable[elemIndex][nodeCount];
        int nodeIndexPrev = meshFEDirectElemTable[elemIndex][nodeCountPrev];
        int nodeIndexNext = meshFEDirectElemTable[elemIndex][nodeCountNext];

        /// Flag on whether the node and its neighbour is inside the current NURBS patch
        bool isNodeInsidePatch = projectedCoords[nodeIndex].find(patchIndex)
                != projectedCoords[nodeIndex].end();
        bool isPrevNodeInsidePatch = projectedCoords[nodeIndexPrev].find(patchIndex)
                != projectedCoords[nodeIndexPrev].end();
        bool isNextNodeInsidePatch = projectedCoords[nodeIndexNext].find(patchIndex)
                != projectedCoords[nodeIndexNext].end();

        /// Get the location of the node and its neighbour
        double* P0 = &(meshFE->nodes[nodeIndexPrev * 3]);
        double* P1 = &(meshFE->nodes[nodeIndex * 3]);
        double* P2 = &(meshFE->nodes[nodeIndexNext * 3]);

        /// Case selection
        /// Node inside
        if(isNodeInsidePatch) {
            u = projectedCoords[nodeIndex][patchIndex][0];
            v = projectedCoords[nodeIndex][patchIndex][1];
            polygonUV.push_back(make_pair(u,v));
            continue;
        }
        /// Node outside and both neighbors inside
        if(!isNodeInsidePatch && isPrevNodeInsidePatch && isNextNodeInsidePatch) {
            double u0In = projectedCoords[nodeIndexPrev][patchIndex][0];
            double v0In = projectedCoords[nodeIndexPrev][patchIndex][1];
            u = u0In;
            v = v0In;
            dis = projectionProperties.maxProjectionDistance;
            isProjectedOnPatchBoundary = projectLineOnPatchBoundary(thePatch, u, v, div, dis, P0, P1);
            double u0=u,v0=v,div0=div;
            double u2In = projectedCoords[nodeIndexNext][patchIndex][0];
            double v2In = projectedCoords[nodeIndexNext][patchIndex][1];
            u = u2In;
            v = v2In;
            dis = projectionProperties.maxProjectionDistance;
            isProjectedOnPatchBoundary = projectLineOnPatchBoundary(thePatch, u, v, div, dis, P2, P1);
            double u2=u,v2=v,div2=div;
            double denominator = (u0In-u0)*(v2In-v2)-(v0In-v0)*(u2In-u2);
            /// If two valid line parameter found and the denominator is valid
            if(div0 >= toleranceRatio && div2 >= toleranceRatio && fabs(denominator) > toleranceRatio) {
                // Compute intersection of the two lines
                // See http://en.wikipedia.org/wiki/Line%E2%80%93line_intersection
                u = ((u0In*v0-v0In*u0)*(u2In-u2)-(u0In-u0)*(u2In*v2-v2In*u2))/denominator;
                v = ((u0In*v0-v0In*u0)*(v2In-v2)-(v0In-v0)*(u2In*v2-v2In*u2))/denominator;
                // Store the point
                polygonUV.push_back(make_pair(u,v));
                continue;
                // If only first line parameter valid
            } else if(div0 >= toleranceRatio) {
                /// Save data from first projection
                u = u0;
                v = v0;
                uIn=u0In;
                vIn=v0In;
                div = div0;
                // If only second line parameter valid
            } else if(div2 >= toleranceRatio) {
                // Save data from second projection
                u = u2;
                v = v2;
                uIn=u2In;
                vIn=v2In;
                div = div2;
            }
            /// Else no valid point found
        }
        /// Node outside and previous neighbor outside and next neighbor inside
        if(!isNodeInsidePatch && !isPrevNodeInsidePatch && isNextNodeInsidePatch) {
            // Set up initial guess from next node
            uIn = projectedCoords[nodeIndexNext][patchIndex][0];
            vIn = projectedCoords[nodeIndexNext][patchIndex][1];
            u = uIn;
            v = vIn;
            dis = projectionProperties.maxProjectionDistance;
            // Project on boundary
            isProjectedOnPatchBoundary = projectLineOnPatchBoundary(thePatch, u, v, div, dis, P2, P1);
        }
        /// Node outside and previous neighbor outside and next neighbor inside
        if(!isNodeInsidePatch && isPrevNodeInsidePatch && !isNextNodeInsidePatch) {
            // Set up initial guess from previous node
            uIn = projectedCoords[nodeIndexPrev][patchIndex][0];
            vIn = projectedCoords[nodeIndexPrev][patchIndex][1];
            u = uIn;
            v = vIn;
            dis = projectionProperties.maxProjectionDistance;
            // Project on boundary
            isProjectedOnPatchBoundary = projectLineOnPatchBoundary(thePatch, u, v, div, dis, P0, P1);
        }
        /// Node outside and both neighbor outside  or no valid line parameter div computed before
        if(div < toleranceRatio) {
            // Project on boundary with all possible inside node until line parameter is valid
            for(vector<int>::iterator it = insideNode.begin(); it != insideNode.end() && div < toleranceRatio; it++) {
                if(*it == nodeIndexPrev || *it == nodeIndexNext)
                    continue;
                double* P0 = &(meshFE->nodes[*it * 3]);
                uIn = projectedCoords[*it][patchIndex][0];
                vIn = projectedCoords[*it][patchIndex][1];
                u = uIn;
                v = vIn;
                dis = projectionProperties.maxProjectionDistance;
                isProjectedOnPatchBoundary = projectLineOnPatchBoundary(thePatch, u, v, div, dis, P0, P1);
            }
        }
        /// Add point in polygon if line parameter is valid
        if(div >= toleranceRatio) {
            u = uIn + (u-uIn)/div;
            v = vIn + (v-vIn)/div;
            polygonUV.push_back(make_pair(u,v));
        }
        /// Warning/Error output
        if(!isProjectedOnPatchBoundary) {
            if(thePatch->isTrimmed()) {
                WARNING_OUT() << "Warning in IGAMortarMapper::buildBoundaryParametricElement"
                              << endl;
                WARNING_OUT() << "Cannot find point projection on patch boundary. "
                              << "Element "<< elemIndex <<" on Patch "<< patchIndex <<" not integrated and skipped !" << endl;
                break;//break loop over node
            } else {
                ERROR_OUT() << "Error in IGAMortarMapper::computeCouplingMatrices"
                            << endl;
                ERROR_OUT() << "Cannot find point projection on patch boundary" << endl;
                ERROR_OUT()
                        << "Cannot find point projection on patch boundary between node ["
                        << nodeIndex << "]:(" << meshFE->nodes[nodeIndex * 3] << ","
                        << meshFE->nodes[nodeIndex * 3 + 1] << ","
                        << meshFE->nodes[nodeIndex * 3 + 2] << ") and node ["
                        << nodeIndexNext << "]:(" << meshFE->nodes[nodeIndexNext * 3]
                        << "," << meshFE->nodes[nodeIndexNext * 3 + 1] << ","
                        << meshFE->nodes[nodeIndexNext * 3 + 2] << ") on patch ["
                        << patchIndex << "] boundary" << endl;
                ERROR_OUT() << "Projection failed in IGA mapper " << name << endl;
                exit(EXIT_FAILURE);
            }
        }
    }
}

bool IGAMortarMapper::projectLineOnPatchBoundary(IGAPatchSurface* thePatch, double& u, double& v, double& div, double& dis, double* Pin, double* Pout) {
    double uIn = u;
    double vIn = v;
    bool isProjectedOnPatchBoundary = thePatch->computePointProjectionOnPatchBoundaryNewtonRhapson(u, v, div, dis, Pin, Pout,
                                                                                                   newtonRaphsonBoundary.maxNumOfIterations, newtonRaphsonBoundary.tolerance);
    if(!isProjectedOnPatchBoundary || dis > projectionProperties.maxProjectionDistance) {
        WARNING_OUT() << "In IGAMortarMapper::projectLineOnPatchBoundary. Point projection on boundary using Newton-Rhapson did not converge. Trying bisection algorithm."<<endl;
        // Reset initial guess
        u = uIn;
        v = vIn;
        isProjectedOnPatchBoundary = thePatch->computePointProjectionOnPatchBoundaryBisection(u, v, div, dis, Pin, Pout,
                                                                                              bisection.maxNumOfIterations, bisection.tolerance);
    }
    // Perform some validity check
    if(!isProjectedOnPatchBoundary)
        WARNING_OUT() << "In IGAMortarMapper::projectLineOnPatchBoundary. Point projection on boundary did not converge. Relax newtonRaphsonBoundary and/or bisection parameters in XML input!"<<endl;
    if(isProjectedOnPatchBoundary && dis > projectionProperties.maxProjectionDistance) {
        WARNING_OUT() << "IGAMortarMapper::projectLineOnPatchBoundary. Point projection on boundary found too far. Distance to edge is "<<dis<<" for prescribed max of "<<projectionProperties.maxProjectionDistance<<". Relax maxProjectionDistance in XML input!"<<endl;
    }
    return isProjectedOnPatchBoundary;
}

bool IGAMortarMapper::computeLocalCouplingMatrix(const int _elemIndex, const int _patchIndex, Polygon2D& _projectedElement) {
    bool isIntegrated=false;
    // Proceed further if the polygon is valid, i.e. at least a triangle
    if (_projectedElement.size() < 3)
        return isIntegrated;
    IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(_patchIndex);
    Polygon2D projectedElementOnPatch = _projectedElement;
    /// 1.0 Apply patch boundary on polygon
    clipByPatch(thePatch, projectedElementOnPatch);
    ClipperAdapter::cleanPolygon(projectedElementOnPatch);
    // Proceed further if the polygon is valid, i.e. at least a triangle
    if (projectedElementOnPatch.size() < 3)
        return isIntegrated;
    /// 1.1 Init list of trimmed polyggons in case patch is not trimmed
    ListPolygon2D listTrimmedPolygonUV(1, projectedElementOnPatch);
    /// 1.2 Apply trimming
    if(thePatch->isTrimmed())
        clipByTrimming(thePatch,projectedElementOnPatch,listTrimmedPolygonUV);
    /// Debug data
    trimmedProjectedPolygons[_patchIndex].insert(trimmedProjectedPolygons[_patchIndex].end(),listTrimmedPolygonUV.begin(), listTrimmedPolygonUV.end());
    /// 1.3 For each subelement output of the trimmed polygon, clip by knot span
    for(int trimmedPolygonIndex=0;trimmedPolygonIndex<listTrimmedPolygonUV.size();trimmedPolygonIndex++) {
        Polygon2D listSpan;
        ListPolygon2D listPolygonUV;
        /// 1.3.1 Clip by knot span
        clipByKnotSpan(thePatch,listTrimmedPolygonUV[trimmedPolygonIndex],listPolygonUV,listSpan);
        /// 1.3.2 For each subelement clipped by knot span, compute canonical element and integrate
        for(int index=0;index<listSpan.size();index++) {
            // ClipperAdapter::cleanPolygon(listPolygonUV[index],1e-9);
            if(listPolygonUV[index].size()<3)
                continue;
            isIntegrated=true;
            ListPolygon2D triangulatedPolygons = triangulatePolygon(listPolygonUV[index]);
            /// 1.3.3 For each triangle, compute canonical element and integrate
            for(ListPolygon2D::iterator triangulatedPolygon=triangulatedPolygons.begin();
                triangulatedPolygon != triangulatedPolygons.end(); triangulatedPolygon++) {
                /// WARNING hard coded tolerance. Cleaning of triangle. Avoid heavily distorted triangle to go further.
                ClipperAdapter::cleanPolygon(*triangulatedPolygon,1e-8);
                if(triangulatedPolygon->size()<3)
                    continue;
                triangulatedProjectedPolygons[_elemIndex][_patchIndex].push_back(*triangulatedPolygon);
                triangulatedProjectedPolygons2[_patchIndex].push_back(*triangulatedPolygon);
                // Get canonical element
                Polygon2D polygonWZ = computeCanonicalElement(_elemIndex, _projectedElement, *triangulatedPolygon);
                // Integrate
                integrate(thePatch,*triangulatedPolygon,listSpan[index].first,listSpan[index].second,polygonWZ,_elemIndex);
            }
        }
    }
    return isIntegrated;
}

void IGAMortarMapper::clipByPatch(const IGAPatchSurface* _thePatch, Polygon2D& _polygonUV) {
    const double u0 = _thePatch->getIGABasis()->getUBSplineBasis1D()->getFirstKnot();
    const double v0 = _thePatch->getIGABasis()->getVBSplineBasis1D()->getFirstKnot();
    const double u1 = _thePatch->getIGABasis()->getUBSplineBasis1D()->getLastKnot();
    const double v1 = _thePatch->getIGABasis()->getVBSplineBasis1D()->getLastKnot();
    Polygon2D knotSpanWindow(4);
    knotSpanWindow[0]=make_pair(u0,v0);
    knotSpanWindow[1]=make_pair(u1,v0);
    knotSpanWindow[2]=make_pair(u1,v1);
    knotSpanWindow[3]=make_pair(u0,v1);
    ClipperAdapter c;
    _polygonUV = c.clip(_polygonUV,knotSpanWindow);
}

void IGAMortarMapper::clipByTrimming(const IGAPatchSurface* _thePatch, const Polygon2D& _polygonUV, ListPolygon2D& _listPolygonUV) {
    ClipperAdapter c;
    // Fill clipper with trimming clipping curves
    for(int loop=0;loop<_thePatch->getTrimming().getNumOfLoops();loop++) {
        const std::vector<double> clippingWindow=_thePatch->getTrimming().getLoop(loop).getPolylines();
        c.addPathClipper(clippingWindow);
    }
    // Setup filling rule to have for sure clockwise loop as hole and counterclockwise as boundaries
    c.setFilling(ClipperAdapter::POSITIVE, 0);
    c.addPathSubject(_polygonUV);
    c.clip();
    c.getSolution(_listPolygonUV);
}

void IGAMortarMapper::clipByKnotSpan(const IGAPatchSurface* _thePatch, const Polygon2D& _polygonUV, ListPolygon2D& _listPolygon, Polygon2D& _listSpan) {
    /// 1.find the knot span which the current element located in.
    //      from minSpanu to maxSpanu in U-direction, and from minSpanV to max SpanV in V-direction
    const double *knotVectorU = _thePatch->getIGABasis()->getUBSplineBasis1D()->getKnotVector();
    const double *knotVectorV = _thePatch->getIGABasis()->getVBSplineBasis1D()->getKnotVector();

    int span[4];
    int isOnSameKnotSpan = computeKnotSpanOfProjElement(_thePatch, _polygonUV,span);
    int minSpanU = span[0];
    int maxSpanU = span[1];
    int minSpanV = span[2];
    int maxSpanV = span[3];
    /// If on same knot span then returned the same polygon as input
    if (isOnSameKnotSpan) {
        _listPolygon.push_back(_polygonUV);
        _listSpan.push_back(make_pair(minSpanU,minSpanV));
        /// Else clip the polygon for every knot span window it is crossing
    } else {
        for (int spanU = minSpanU; spanU <= maxSpanU; spanU++) {
            for (int spanV = minSpanV; spanV <= maxSpanV; spanV++) {
                /// WARNING hard coded tolerance. Uses reduced clipping tolerance (initially 1e-12). Avoid numerical instability on knot span inside/outside.
                ClipperAdapter c(1e-9);
                if (knotVectorU[spanU] != knotVectorU[spanU + 1]
                        && knotVectorV[spanV] != knotVectorV[spanV + 1]) {
                    Polygon2D knotSpanWindow(4);
                    knotSpanWindow[0]=make_pair(knotVectorU[spanU],knotVectorV[spanV]);
                    knotSpanWindow[1]=make_pair(knotVectorU[spanU+1],knotVectorV[spanV]);
                    knotSpanWindow[2]=make_pair(knotVectorU[spanU+1],knotVectorV[spanV+1]);
                    knotSpanWindow[3]=make_pair(knotVectorU[spanU],knotVectorV[spanV+1]);
                    /// WARNING design. Here we assume to get only a single output polygon from the clipping !
                    Polygon2D solution = c.clip(_polygonUV,knotSpanWindow);

                    /// Store polygon and its knot span for integration
                    _listPolygon.push_back(solution);
                    _listSpan.push_back(make_pair(spanU,spanV));
                }
            }
        }
    }
}

IGAMortarMapper::ListPolygon2D IGAMortarMapper::triangulatePolygon(const Polygon2D& _polygonUV) {
    // If already easily integrable by quadrature rule, do nothing
    if(_polygonUV.size()<4)
        return ListPolygon2D(1,_polygonUV);
    // Otherwise triangulate polygon
    TriangulatorAdaptor triangulator;
    // Fill adapter
    for(Polygon2D::const_iterator it=_polygonUV.begin();it!=_polygonUV.end();it++)
        triangulator.addPoint(it->first,it->second,0);
    // Triangulate
    int numTriangles = _polygonUV.size() - 2;
    int triangleIndexes[3 * numTriangles];
    bool triangulated=triangulator.triangulate(triangleIndexes);
    if(!triangulated)
        return ListPolygon2D();
    // Fill output structure
    ListPolygon2D out(numTriangles, Polygon2D(3));
    for(int i = 0; i < numTriangles; i++)
        for(int j=0;j<3;j++)
            out[i][j]=_polygonUV[triangleIndexes[3*i + j]];
    return out;
}

IGAMortarMapper::Polygon2D IGAMortarMapper::computeCanonicalElement(const int _elementIndex, const Polygon2D& _theElement, const Polygon2D& _polygonUV) {
    int numNodesElementFE = meshFE->numNodesPerElem[_elementIndex];
    double elementFEUV[8], coordsNodeFEUV[2], coordsNodeFEWZ[2];
    for(int i = 0; i < numNodesElementFE; i++) {
        elementFEUV[2*i]   = _theElement[i].first;
        elementFEUV[2*i+1] = _theElement[i].second;
    }
    // Compute canonical coordinates polygon using parametric space
    Polygon2D polygonWZ;
    for(int i=0; i<_polygonUV.size(); i++) {
        coordsNodeFEUV[0] = _polygonUV[i].first;
        coordsNodeFEUV[1] = _polygonUV[i].second;
        if(numNodesElementFE == 3)
            MathLibrary::computeLocalCoordsInTriangle(elementFEUV, coordsNodeFEUV, coordsNodeFEWZ);
        else
            MathLibrary::computeLocalCoordsInQuad(elementFEUV, coordsNodeFEUV, coordsNodeFEWZ);
        polygonWZ.push_back(make_pair(coordsNodeFEWZ[0],coordsNodeFEWZ[1]));
    }
    return polygonWZ;
}

void IGAMortarMapper::integrate(IGAPatchSurface* _thePatch, Polygon2D _polygonUV,
                                int _spanU, int _spanV, Polygon2D _polygonWZ, int _elementIndex) {
    /*
     * 1. Divide the polygon into several quadratures(triangle or quadriliteral) for integration
     * 2. Loop through each quadrature
     *   2.1 Choose a Gauss quadrature (triangle or quadriliteral)
     *   2.2 Loop throught each Gauss point
     *       2.2.1 compute shape functions from Gauss points
     *       2.2.2 evaluate the coordinates in IGA patch from shape functions
     *       2.2.3 evaluate the coordinates in the linear element from shape functions
     *       2.2.4 compute the shape functions in the linear element for the current integration point
     *       2.2.5 Compute the local basis functions(shape functions of IGA) and their derivatives(for Jacobian)
     *       2.2.6 Compute the Jacobian from parameter space on IGA patch to physical
     *       2.2.7 Compute the Jacobian from the canonical space to the parameter space of IGA patch
     *       2.2.8 integrate the shape function product for C_NN(Linear shape function multiply linear shape function)
     *       2.2.9 integrate the shape function product for C_NR(Linear shape function multiply IGA shape function)
     * 3. Assemble the element coupling matrix to the global coupling matrix.
     */

    assert(!_polygonUV.empty());
    assert(!_polygonWZ.empty());

    int numNodesUV=_polygonUV.size();
    int numNodesWZ=_polygonWZ.size();
    assert(numNodesUV>2);
    assert(numNodesUV<5);
    assert(numNodesWZ>2);
    assert(numNodesWZ<5);


    // Definitions
    int numNodesElementFE=meshFE->numNodesPerElem[_elementIndex];
    int numNodesElMaster = 0;
    int numNodesElSlave = 0;

    int pDegree = _thePatch->getIGABasis()->getUBSplineBasis1D()->getPolynomialDegree();
    int qDegree = _thePatch->getIGABasis()->getVBSplineBasis1D()->getPolynomialDegree();
    int nShapeFuncsIGA = (pDegree + 1) * (qDegree + 1);

    if (isMappingIGA2FEM) {
        numNodesElMaster = numNodesElementFE;
        numNodesElSlave = nShapeFuncsIGA;
    } else {
        numNodesElMaster = nShapeFuncsIGA;
        numNodesElSlave = numNodesElementFE;
    }
    // Initialize element matrix
    double elementCouplingMatrixNN[numNodesElMaster * (numNodesElMaster + 1) / 2];
    double elementCouplingMatrixNR[numNodesElSlave * numNodesElMaster];

    for (int arrayIndex = 0; arrayIndex < numNodesElMaster * (numNodesElMaster + 1) / 2;
         arrayIndex++)
        elementCouplingMatrixNN[arrayIndex] = 0;

    for (int arrayIndex = 0; arrayIndex < numNodesElSlave * numNodesElMaster; arrayIndex++)
        elementCouplingMatrixNR[arrayIndex] = 0;

    int dofIGA[nShapeFuncsIGA];
    _thePatch->getIGABasis()->getBasisFunctionsIndex(_spanU, _spanV, dofIGA);

    for (int i = 0; i < nShapeFuncsIGA; i++)
        dofIGA[i] = _thePatch->getControlPointNet()[dofIGA[i]]->getDofIndex();

    /// 1. Copy input polygon into contiguous C format
    double nodesUV[8];
    double nodesWZ[8];
    for (int i = 0; i < numNodesUV; i++) {
        nodesUV[i*2]=_polygonUV[i].first;
        nodesUV[i*2+1]=_polygonUV[i].second;
        nodesWZ[i*2]=_polygonWZ[i].first;
        nodesWZ[i*2+1]=_polygonWZ[i].second;
    }

    /// 2. Loop through each quadrature
    /// 2.1 Choose gauss triangle or gauss quadriliteral
    MathLibrary::IGAGaussQuadrature *theGaussQuadrature;
    int nNodesQuadrature=numNodesUV;
    if (nNodesQuadrature == 3)
        theGaussQuadrature = gaussTriangle;
    else
        theGaussQuadrature = gaussQuad;

    double *quadratureUV = nodesUV;
    double *quadratureWZ = nodesWZ;

    /// 2.2 Loop throught each Gauss point
    for (int GPCount = 0; GPCount < theGaussQuadrature->numGaussPoints; GPCount++) {

        /// 2.2.1 compute shape functions from Gauss points(in the quadrature).
        const double *GP = theGaussQuadrature->getGaussPoint(GPCount);

        double shapeFuncs[nNodesQuadrature];
        MathLibrary::computeLowOrderShapeFunc(nNodesQuadrature, GP, shapeFuncs);

        /// 2.2.2 evaluate the coordinates in IGA patch from shape functions
        double GPIGA[2];
        MathLibrary::computeLinearCombination(nNodesQuadrature, 2, quadratureUV, shapeFuncs,
                                              GPIGA);

        /// 2.2.3 evaluate the coordinates in the linear element from shape functions
        double GPFE[2];
        MathLibrary::computeLinearCombination(nNodesQuadrature, 2, quadratureWZ, shapeFuncs,
                                              GPFE);

        /// 2.2.4 compute the shape function(in the linear element) of the current integration point
        double shapeFuncsFE[numNodesElementFE];
        MathLibrary::computeLowOrderShapeFunc(numNodesElementFE, GPFE, shapeFuncsFE);

        int derivDegree = 1;

        /// 2.2.5 Compute the local basis functions(shape functions of IGA) and their derivatives(for Jacobian)
        double localBasisFunctionsAndDerivatives[(derivDegree + 1) * (derivDegree + 2)
                * nShapeFuncsIGA / 2];

        _thePatch->getIGABasis()->computeLocalBasisFunctionsAndDerivatives(
                    localBasisFunctionsAndDerivatives, derivDegree, GPIGA[0], _spanU, GPIGA[1],
                _spanV);

        /// 2.2.6 Compute the Jacobian from parameter space on IGA patch to physical
        double baseVectors[6];
        _thePatch->computeBaseVectors(baseVectors, localBasisFunctionsAndDerivatives, _spanU,
                                      _spanV);

        double JacobianUVToPhysical = MathLibrary::computeAreaTriangle(baseVectors[0],
                baseVectors[1], baseVectors[2], baseVectors[3], baseVectors[4], baseVectors[5])
                * 2;

        /// 2.2.7 Compute the Jacobian from the canonical space to the parameter space of IGA patch
        double JacobianCanonicalToUV;
        if (nNodesQuadrature == 3) {
            JacobianCanonicalToUV = MathLibrary::computeAreaTriangle(
                        quadratureUV[2] - quadratureUV[0], quadratureUV[3] - quadratureUV[1], 0,
                    quadratureUV[4] - quadratureUV[0], quadratureUV[5] - quadratureUV[1], 0);
        } else {
            double dudx = .25
                    * (-(1 - GP[2]) * quadratureUV[0] + (1 - GP[2]) * quadratureUV[2]
                    + (1 + GP[2]) * quadratureUV[4] - (1 + GP[2]) * quadratureUV[6]);
            double dudy = .25
                    * (-(1 - GP[1]) * quadratureUV[0] - (1 + GP[1]) * quadratureUV[2]
                    + (1 + GP[1]) * quadratureUV[4] + (1 - GP[1]) * quadratureUV[6]);
            double dvdx = .25
                    * (-(1 - GP[2]) * quadratureUV[1] + (1 - GP[2]) * quadratureUV[3]
                    + (1 + GP[2]) * quadratureUV[5] - (1 + GP[2]) * quadratureUV[7]);
            double dvdy = .25
                    * (-(1 - GP[1]) * quadratureUV[1] - (1 + GP[1]) * quadratureUV[3]
                    + (1 + GP[1]) * quadratureUV[5] + (1 - GP[1]) * quadratureUV[7]);
            JacobianCanonicalToUV = fabs(dudx * dvdy - dudy * dvdx);
        }
        double Jacobian = JacobianUVToPhysical * JacobianCanonicalToUV;
        /// 2.2.8 integrate the shape function product for C_NN(Linear shape function multiply linear shape function)
        int count = 0;
        for (int i = 0; i < numNodesElMaster; i++) {
            for (int j = i; j < numNodesElMaster; j++) {
                if (isMappingIGA2FEM)
                    elementCouplingMatrixNN[count++] += shapeFuncsFE[i] * shapeFuncsFE[j]
                            * Jacobian * theGaussQuadrature->weights[GPCount];
                else {
                    double IGABasisFctsI =
                            localBasisFunctionsAndDerivatives[_thePatch->getIGABasis()->indexDerivativeBasisFunction(
                                1, 0, 0, i)];
                    double IGABasisFctsJ =
                            localBasisFunctionsAndDerivatives[_thePatch->getIGABasis()->indexDerivativeBasisFunction(
                                1, 0, 0, j)];
                    elementCouplingMatrixNN[count++] += IGABasisFctsI * IGABasisFctsJ * Jacobian
                            * theGaussQuadrature->weights[GPCount];
                }
            }
        }
        /// Save GP data
        std::vector<double> streamGP;
        // weight + jacobian + nShapeFuncsFE + (#dof, shapefuncvalue,...) + nShapeFuncsIGA + (#dof, shapefuncvalue,...)
        streamGP.reserve(1 + 1 + 1 + 2*numNodesElementFE + 1 + 2*nShapeFuncsIGA);
        streamGP.push_back(theGaussQuadrature->weights[GPCount]);
        streamGP.push_back(Jacobian);
        streamGP.push_back(numNodesElementFE);
        for (int i = 0; i < numNodesElementFE; i++) {
            streamGP.push_back(meshFEDirectElemTable[_elementIndex][i]);
            streamGP.push_back(shapeFuncsFE[i]);
        }
        streamGP.push_back(nShapeFuncsIGA);
        for (int i = 0; i < nShapeFuncsIGA; i++) {
            double IGABasisFctsI =
                    localBasisFunctionsAndDerivatives[_thePatch->getIGABasis()->indexDerivativeBasisFunction(
                        1, 0, 0, i)];
            streamGP.push_back(dofIGA[i]);
            streamGP.push_back(IGABasisFctsI);
        }
        this->streamGP.push_back(streamGP);
        /// 2.2.9 integrate the shape function product for C_NR(Linear shape function multiply IGA shape function)
        count = 0;
        for (int i = 0; i < numNodesElMaster; i++) {
            for (int j = 0; j < numNodesElSlave; j++) {
                double basisFctsMaster;
                double basisFctsSlave;
                if (isMappingIGA2FEM) {
                    basisFctsMaster = shapeFuncsFE[i];
                    basisFctsSlave =
                            localBasisFunctionsAndDerivatives[_thePatch->getIGABasis()->indexDerivativeBasisFunction(
                                1, 0, 0, j)];
                } else {
                    basisFctsMaster =
                            localBasisFunctionsAndDerivatives[_thePatch->getIGABasis()->indexDerivativeBasisFunction(
                                1, 0, 0, i)];
                    basisFctsSlave = shapeFuncsFE[j];
                }
                elementCouplingMatrixNR[count++] += basisFctsMaster * basisFctsSlave * Jacobian
                        * theGaussQuadrature->weights[GPCount];
            }
        }
    }
    /// 3.Assemble the element coupling matrix to the global coupling matrix.
    ///Create new scope
    {
        int dofIGA[nShapeFuncsIGA];
        _thePatch->getIGABasis()->getBasisFunctionsIndex(_spanU, _spanV, dofIGA);

        for (int i = 0; i < nShapeFuncsIGA; i++)
            dofIGA[i] = _thePatch->getControlPointNet()[dofIGA[i]]->getDofIndex();

        int count = 0;
        int dof1, dof2;
        /// 3.1 Assemble Cnn
        for (int i = 0; i < numNodesElMaster; i++)
            for (int j = i; j < numNodesElMaster; j++) {
                if (isMappingIGA2FEM) {
                    dof1 = meshFEDirectElemTable[_elementIndex][i];
                    dof2 = meshFEDirectElemTable[_elementIndex][j];
                } else {
                    dof1 = dofIGA[i];
                    dof2 = dofIGA[j];
                }
                couplingMatrices->addCNNValue(dof1,dof2,elementCouplingMatrixNN[count]);
                if (dof1 != dof2)
                    couplingMatrices->addCNNValue(dof2,dof1,elementCouplingMatrixNN[count]);
                count++;
            }

        count = 0;
        /// 3.1 Assemble Cnr
        for (int i = 0; i < numNodesElMaster; i++)
            for (int j = 0; j < numNodesElSlave; j++) {
                if (isMappingIGA2FEM) {
                    dof1 = meshFEDirectElemTable[_elementIndex][i];
                    dof2 = dofIGA[j];
                } else {
                    dof1 = dofIGA[i];
                    dof2 = meshFEDirectElemTable[_elementIndex][j];
                }
                couplingMatrices->addCNRValue(dof1,dof2,elementCouplingMatrixNR[count]);
                count ++;
            }
    }
}

bool IGAMortarMapper::computeKnotSpanOfProjElement(const IGAPatchSurface* _thePatch, const Polygon2D& _polygonUV, int* _span) {
    int minSpanU = _thePatch->getIGABasis()->getUBSplineBasis1D()->findKnotSpan(
                _polygonUV[0].first);
    int minSpanV = _thePatch->getIGABasis()->getVBSplineBasis1D()->findKnotSpan(
                _polygonUV[0].second);
    int maxSpanU = minSpanU;
    int maxSpanV = minSpanV;

    for (int nodeCount = 1; nodeCount < _polygonUV.size(); nodeCount++) {
        int spanU = _thePatch->getIGABasis()->getUBSplineBasis1D()->findKnotSpan(_polygonUV[nodeCount].first);
        int spanV = _thePatch->getIGABasis()->getVBSplineBasis1D()->findKnotSpan(_polygonUV[nodeCount].second);

        if (spanU < minSpanU)
            minSpanU = spanU;
        if (spanU > maxSpanU)
            maxSpanU = spanU;
        if (spanV < minSpanV)
            minSpanV = spanV;
        if (spanV > maxSpanV)
            maxSpanV = spanV;
    }

    bool OnSameKnotSpan=(minSpanU == maxSpanU && minSpanV == maxSpanV);
    if(_span!=NULL) {
        _span[0]=minSpanU;
        _span[1]=maxSpanU;
        _span[2]=minSpanV;
        _span[3]=maxSpanV;
    }
    return OnSameKnotSpan;
}

int IGAMortarMapper::getNeighbourElementofEdge(int _element, int _node1, int _node2) {
    for(int i=0; i<meshFE->numElems;i++) {
        bool isNode1=false;
        bool isNode2=false;
        for(int j=0;j<meshFE->numNodesPerElem[i];j++) {
            if(isNode1==false)
                isNode1=(meshFEDirectElemTable[i][j]==_node1)?true:false;
            if(isNode2==false)
                isNode2=(meshFEDirectElemTable[i][j]==_node2)?true:false;
            if(_element!=i && isNode1 && isNode2) {
                return i;
            }
        }
    }
    // If polygon is on boundary of mesh, can occur
    return -1;
}

void IGAMortarMapper::consistentMapping(const double* _slaveField, double *_masterField) {
    /*
     * Mapping of the
     * C_NN * x_master = C_NR * x_slave
     */
    int size_N = couplingMatrices->getCorrectSizeN();
    int size_R = couplingMatrices->getCorrectSizeR();

    double* tmpVec = new double[size_N]();
    couplingMatrices->getCorrectCNR()->mulitplyVec(false,const_cast<double *>(_slaveField), tmpVec, size_N);

    couplingMatrices->getCorrectCNN()->solve(_masterField, tmpVec);

    delete[] tmpVec;
}

void IGAMortarMapper::conservativeMapping(const double* _masterField, double *_slaveField) {
    /*
     * Mapping of the
     * f_slave = (C_NN^(-1) * C_NR)^T * f_master
     */
    int size_N = couplingMatrices->getCorrectSizeN();
    int size_R = couplingMatrices->getCorrectSizeR();

    double* tmpVec = new double[size_N];

    couplingMatrices->getCorrectCNN_conservative()->solve(tmpVec, const_cast<double *>(_masterField));

    couplingMatrices->getCorrectCNR_conservative()->transposeMulitplyVec(tmpVec, _slaveField, numNodesMaster);

    delete[] tmpVec;
}

void IGAMortarMapper::writeGaussPointData() {
    string filename = name + "_GaussPointData.csv";
    ofstream filestream;
    filestream.open(filename.c_str());
    filestream.precision(12);
    filestream << std::dec;
    for(std::vector<std::vector<double> >::iterator it1 = streamGP.begin(); it1!=streamGP.end(); it1++) {
        for(std::vector<double>::iterator it2=it1->begin(); it2!=it1->end(); it2++) {
            filestream << *it2 << " ";
        }
        filestream << endl;
    }
    filestream.close();
}

void IGAMortarMapper::writeProjectedNodesOntoIGAMesh() {
    // Initializations
    IGAPatchSurface* IGAPatch;
    int numXiKnots, numEtaKnots, numXiControlPoints, numEtaControlPoints;
    double xiKnot, etaKnot;

    // Open file for writing the projected nodes
    ofstream projectedNodesFile;
    const string UNDERSCORE = "_";
    string projectedNodesFileName = name + "_projectedNodesOntoNURBSSurface.m";
    projectedNodesFile.open(projectedNodesFileName.c_str());
    projectedNodesFile.precision(14);
    projectedNodesFile << std::dec;

    projectedNodesFile << HEADER_DECLARATION << endl << endl;

    // Loop over all the patches
    for (int patchCounter = 0; patchCounter < meshIGA->getNumPatches(); patchCounter++) {
        // Get the IGA patch
        IGAPatch = meshIGA->getSurfacePatches()[patchCounter];

        // Get number of knots in each parametric direction
        numXiKnots = IGAPatch->getIGABasis()->getUBSplineBasis1D()->getNoKnots();
        numEtaKnots = IGAPatch->getIGABasis()->getVBSplineBasis1D()->getNoKnots();

        // Write out the knot vector in u-direction
        projectedNodesFile << "Patch" << patchCounter << endl << endl;
        projectedNodesFile << "xiKnotVector" << endl;

        for (int xiCounter = 0; xiCounter < numXiKnots; xiCounter++) {
            xiKnot = IGAPatch->getIGABasis()->getUBSplineBasis1D()->getKnotVector()[xiCounter];
            projectedNodesFile << xiKnot << " ";
        }
        projectedNodesFile << endl << endl;

        // Write out the knot vector in v-direction
        projectedNodesFile << "etaKnotVector" << endl;
        for (int etaCounter = 0; etaCounter < numEtaKnots; etaCounter++) {
            etaKnot = IGAPatch->getIGABasis()->getVBSplineBasis1D()->getKnotVector()[etaCounter];
            projectedNodesFile << etaKnot << " ";
        }
        projectedNodesFile << endl << endl;

        // Loop over all the nodes
        for (int nodeIndex = 0; nodeIndex < meshFE->numNodes; nodeIndex++) {
            // Loop over all the patches on which this node has been successfully projected
            for (map<int, vector<double> >::iterator it = projectedCoords[nodeIndex].begin();
                 it != projectedCoords[nodeIndex].end(); it++)
                if (it->first == patchCounter) {
                    projectedNodesFile << nodeIndex << "\t" << it->first << "\t" << it->second[0]
                                       << "\t" << it->second[1] << endl;
                }
        }
        projectedNodesFile << endl;
    }

    // Close file
    projectedNodesFile.close();
}

void IGAMortarMapper::writeParametricProjectedPolygons(string _filename) {
    string filename = name + "_" + _filename + ".csv";
    ofstream out;
    out.open(filename.c_str(), ofstream::out);
    for(int i=0;i<projectedPolygons.size();i++) {
        for(map<int,Polygon2D>::const_iterator it=projectedPolygons[i].begin(); it!=projectedPolygons[i].end(); it++) {
            out << i << "\t" << it->first;
            for(int j=0; j<it->second.size(); j++) {
                out<<"\t"<<(it->second)[j].first<<"\t"<<(it->second)[j].second;
            }
            out << endl;
        }
    }
    out.close();
}

void IGAMortarMapper::writeTriangulatedParametricPolygon(string _filename) {
    string filename = name + "_" + _filename + ".csv";
    ofstream out;
    out.open(filename.c_str(), ofstream::out);
    for(int i=0;i<triangulatedProjectedPolygons.size();i++) {
        for(map<int,ListPolygon2D>::const_iterator it1=triangulatedProjectedPolygons[i].begin(); it1!=triangulatedProjectedPolygons[i].end(); it1++) {
            out << i << "\t" << it1->first;
            for(ListPolygon2D::const_iterator it2=it1->second.begin(); it2!=it1->second.end(); it2++) {
                for(int j=0; j<it2->size(); j++) {
                    out<<"\t"<<(*it2)[j].first<<"\t"<<(*it2)[j].second;
                }
            }
            out << endl;
        }
    }
    out.close();
}

void IGAMortarMapper::writeCartesianProjectedPolygon(const string _filename, std::map<int, ListPolygon2D>& _data) {
    ofstream out;
    string outName = name + "_" +_filename + ".vtk";
    out.open(outName.c_str(), ofstream::out);
    out << "# vtk DataFile Version 2.0\n";
    out << "Back projection of projected FE elements on NURBS mesh\n";
    out << "ASCII\nDATASET POLYDATA\n";
    string points, pointsHeader, polygons, polygonsHeader, patchColor, patchColorHeader;
    int pointsNumber=0, polygonsNumber=0, polygonsEntriesNumber=0;
    /// Loop over data
    for(map<int,ListPolygon2D>::iterator itPatch=_data.begin(); itPatch!=_data.end(); itPatch++) {
        int idPatch = itPatch->first;
        IGAPatchSurface* thePatch = meshIGA->getSurfacePatch(idPatch);
        for(ListPolygon2D::iterator itListPolygon=itPatch->second.begin(); itListPolygon!=itPatch->second.end(); itListPolygon++) {
            int nEdge=0;
            for(Polygon2D::iterator itPolygon=itListPolygon->begin(); itPolygon!=itListPolygon->end(); itPolygon++) {
                double local[2], global[3];
                local[0] = itPolygon->first;
                local[1] = itPolygon->second;
                thePatch->computeCartesianCoordinates(global,local);
                stringstream pointStream;
                pointStream << global[0];
                pointStream << " " << global[1];
                pointStream << " " << global[2];
                pointStream << "\n";
                points += pointStream.str();
                pointsNumber++;
                nEdge++;
            }
            stringstream colorStream;
            /// Concatenate new polygon color
            colorStream << idPatch << "\n";
            patchColor += colorStream.str();
            polygonsNumber++;
            polygonsEntriesNumber += nEdge + 1;
            /// Concatenate new polygon connectivity
            stringstream polygonStream;
            polygonStream << nEdge;
            for(int i=nEdge;i>0;i--) {
                polygonStream << " " << pointsNumber - i;
            }
            polygonStream << "\n";
            polygons += polygonStream.str();
        }
    }
    /// Write actually the file
    stringstream header;
    header << "POINTS " << pointsNumber << " float\n";
    pointsHeader = header.str();
    header.str("");
    header.clear();
    header << "POLYGONS " << polygonsNumber << " " << polygonsEntriesNumber <<"\n";
    polygonsHeader = header.str();
    header.str("");
    header.clear();
    header << "CELL_DATA " << polygonsNumber << "\nSCALARS patch_belonging int 1\nLOOKUP_TABLE default\n";
    patchColorHeader = header.str();
    out << pointsHeader << points;
    out << polygonsHeader << polygons;
    out << patchColorHeader << patchColor;
    out.close();
}

void IGAMortarMapper::debugPolygon(const Polygon2D& _polygon, string _name) {
    DEBUG_OUT()<<"----------------------------------"<<endl;
    if(_name!="")
        DEBUG_OUT()<<"Polygon name : "<<_name<<endl;
    for(int i=0; i<_polygon.size();i++)
        DEBUG_OUT()<<"\t"<<"u="<<_polygon[i].first<<" / v="<<_polygon[i].second<<endl;
    DEBUG_OUT()<<"----------------------------------"<<endl;
}
void IGAMortarMapper::debugPolygon(const ListPolygon2D& _listPolygon, string _name) {
    DEBUG_OUT()<<"++++++++++++++++++++++++++"<<endl;
    if(_name!="")
        DEBUG_OUT()<<"Polygon list name : "<<_name<<endl;
    for(int i=0; i<_listPolygon.size();i++) {
        DEBUG_OUT()<<"Polygon index : "<<i<<endl;
        debugPolygon(_listPolygon[i]);
    }
    DEBUG_OUT()<<"++++++++++++++++++++++++++"<<endl;
}

void IGAMortarMapper::printCouplingMatrices() {

    ERROR_OUT() << "C_NN" << endl;
    couplingMatrices->getCorrectCNN()->printCSR();
    ERROR_OUT() << "C_NR" << endl;
    couplingMatrices->getCorrectCNR()->printCSR();
}

void IGAMortarMapper::writeCouplingMatricesToFile() {
    DEBUG_OUT()<<"### Printing matrices into file ###"<<endl;
    DEBUG_OUT()<<"Size of C_NR is "<<numNodesMaster<<" by "<<numNodesSlave<<endl;
    if(Message::isDebugMode()) {
        couplingMatrices->getCorrectCNR()->printCSRToFile(name + "_Cnr.dat",1);
        couplingMatrices->getCorrectCNN()->printCSRToFile(name + "_Cnn.dat",1);
    }
}

void IGAMortarMapper::checkConsistency() {
    INFO_OUT()<<"Check Consistency"<<std::endl;

    int size_N = couplingMatrices->getCorrectSizeN();
    int size_R = couplingMatrices->getCorrectSizeR();

    double ones[size_R];
    for(int i=0;i<size_R;i++) {
        ones[i]=1.0;
    }

    double output[size_N];
    this->consistentMapping(ones,output);

    double norm=0;
    vector<int> inconsistentDoF;

    for(int i=0;i<size_N;i++) {
        if(fabs(output[i]-1) > 1e-6 && output[i] != 0)
            inconsistentDoF.push_back(i);
        norm+=output[i]*output[i];
    }

    // Replace badly conditioned row of Cnn by sum value of Cnr
    if(!inconsistentDoF.empty()) {
        INFO_OUT()<<"inconsistendDOF size = "<<inconsistentDoF.size()<<std::endl;
        for(vector<int>::iterator it=inconsistentDoF.begin();it!=inconsistentDoF.end();it++) {
            if(!useIGAPatchCouplingPenalties) {
                couplingMatrices->deleterow(*it);       // deleterow might not be working for the new coupling matrix datastructure
                couplingMatrices->addCNNValue(*it ,*it , couplingMatrices->getCorrectCNR()->getRowSum(*it));
            }
            else {
                couplingMatrices->deleterow(*it); // deleterow might not be working for the new coupling matrix datastructure
                couplingMatrices->addCNN_expandedValue(*it ,*it , couplingMatrices->getCorrectCNR()->getRowSum(*it));
            }
        }

        couplingMatrices->factorizeCorrectCNN();
        this->consistentMapping(ones,output);
        norm=0;

        for(int i=0;i<size_N;i++) {
            norm += output[i]*output[i];
        }
    }

    int denom = size_N - couplingMatrices->getIndexEmptyRowCnn().size();

    norm=sqrt(norm/denom);

    DEBUG_OUT()<<"### Check consistency ###"<<endl;
    DEBUG_OUT()<<"Norm of output field = "<<norm<<endl;
    if(fabs(norm-1.0)>1e-6) {
        ERROR_OUT()<<"Coupling not consistent !"<<endl;
        ERROR_OUT()<<"Coupling of unit field deviating from 1 of "<<fabs(norm-1.0)<<endl;
        exit(-1);
    }
}

}
