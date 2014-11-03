/*  Copyright &copy; 2013, TU Muenchen, Chair of Structural Analysis,
 *  Stefan Sicklinger, Tianyang Wang, Munich
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
/***********************************************************************************************//**
 * \file ApiWrapperFromCppToC.cpp
 * This file wraps the C++ API in a C API
 * \date 2/22/2012
 **************************************************************************************************/
#include "Empire.h"
#include "EMPIRE_API.h"
#include <assert.h>
#include <string>

using namespace EMPIRE;
using namespace std;

/// Lets make a EMPIRE_API object in the global scope
Empire *empire;

void EMPIRE_API_Connect(char* inputFileName) {
    empire = new Empire();
    empire->initEnvironment(inputFileName);
    empire->connect();
}

char *EMPIRE_API_getUserDefinedText(char *elementName) {
    string text = empire->getUserDefinedText(elementName);
    return const_cast<char*>(text.c_str());
}

void EMPIRE_API_sendMesh(char *name, int numNodes, int numElems, double *nodes, int *nodeIDs,
        int *numNodesPerElem, int *elems) {
    empire->sendMesh(numNodes, numElems, nodes, nodeIDs, numNodesPerElem, elems);
}

//altug
void EMPIRE_API_recvMeshInit(char *name, int *numNodes, int *numElems) {

    empire->recvMeshInit(numNodes, numElems);
}

void EMPIRE_API_recvMeshData(char *name, int numNodes, int numElems, double *nodes, int *nodeIDs,
            int *numNodesPerElem) {
    empire->recvMeshData(numNodes,numElems,nodes, nodeIDs, numNodesPerElem);
}

void EMPIRE_API_recvMesh(char *name, int *numNodes, int *numElems, double **nodes, int **nodeIDs,
            int **numNodesPerElem, int **elem) {
    empire->recvMesh(numNodes,numElems,nodes, nodeIDs, numNodesPerElem, elem);
}
//altug

void EMPIRE_API_sendIGAPatch(int _pDegree, int _uNumKnots, double* _uKnotVector, int _qDegree,
        int _vNumKnots, double* _vKnotVector, int _uNumControlPoints, int _vNumControlPoints,
        double* _cpNet, int* _nodeNet) {
    empire->sendIGAPatch(_pDegree, _uNumKnots, _uKnotVector, _qDegree, _vNumKnots, _vKnotVector,
            _uNumControlPoints, _vNumControlPoints, _cpNet, _nodeNet);
}

void EMPIRE_API_sendIGAMesh(char *_name, int _numPatches, int _numNodes) {
    empire->sendIGAMesh(_numPatches, _numNodes);
}

void EMPIRE_API_sendDataField(char *name, int sizeOfArray, double *dataField) {
    empire->sendDataField(sizeOfArray, dataField);
}

void EMPIRE_API_recvDataField(char *name, int sizeOfArray, double *dataField) {
    empire->recvDataField(sizeOfArray, dataField);
}

void EMPIRE_API_sendSignal_double(char *name, int sizeOfArray, double *signal) {
    empire->sendSignal_double(name, sizeOfArray, signal);
}

void EMPIRE_API_recvSignal_double(char *name, int sizeOfArray, double *signal) {
    empire->recvSignal_double(name, sizeOfArray, signal);
}

void EMPIRE_API_sendConvergenceSignal(int signal) {
    empire->sendConvergenceSignal(signal);
}

int EMPIRE_API_recvConvergenceSignal() {
    return empire->recvConvergenceSignal();
}

void EMPIRE_API_printDataField(char *name, int sizeOfArray, double *dataField) {
    empire->printDataField(name, sizeOfArray, dataField);
}

void EMPIRE_API_Disconnect() {
    empire->disconnect();
    delete empire;
}
