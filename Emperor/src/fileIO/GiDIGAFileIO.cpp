/*
 * GiDNurbsFileIO.cpp
 *
 *  Created on: May 13, 2015
 *      Author: Philipp Bucher
 */

#include "GiDIGAFileIO.h"

#include "FEMesh.h"
#include "IGAPatchSurface.h"
#include "BSplineBasis2D.h"
#include "BSplineBasis1D.h"
#include "IGAControlPoint.h"
#include "DataField.h"

#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <assert.h>
#include <vector>
#include "stdlib.h"
#include <math.h>

using namespace std;
namespace EMPIRE {
namespace GiDIGAFileIO {

void writeIGAMesh(const std::string geometryFileName, const EMPIRE::IGAMesh* igaMesh) {
	ofstream myfile;
	myfile.open(geometryFileName.c_str());
	myfile.precision(14);
	myfile << std::dec;

	// start writing data to file
	myfile << headerDotGeo;	// write header

	unsigned int pointCounter = 1;
	unsigned int curveCounter = 1;
	unsigned int surfaceCounter = 1;
	double cartCoord[3], surfCoord[2];
	std::vector<double> polylines;	// vector to store parametric coordinates of linearized NURBS curve

	const int numPatches = igaMesh->getNumPatches();
	for (int indexPatch = 0; indexPatch < numPatches; indexPatch++) {  // loop over patches in IGA-mesh
		const EMPIRE::IGAPatchSurface* patch = igaMesh->getSurfacePatch(indexPatch);
		const int numCPSs = patch->getNoControlPoints();
		const int numLoops = patch->getTrimming().getNumOfLoops();
		unsigned int curveCounterBeginning = curveCounter;

		// ********** trimmed patch **********
		if (patch->getTrimming().isTrimmed()) {
			for (int indexLoop = 0; indexLoop < numLoops; indexLoop++) {  // loop over trimming loops
				const EMPIRE::IGAPatchSurfaceTrimmingLoop* loop =
						&patch->getTrimming().getLoop(indexLoop);
				const int numCurves = loop->getNoCurves();
				int firstLastPoint = curveCounter;

				for (int indexCurve = 0; indexCurve < numCurves; indexCurve++) {  // loop over curves in trimming loop
					const EMPIRE::IGAPatchCurve* curve = &loop->getIGACurve(
							indexCurve);

					linearizeUsingNCPxP(polylines, curve, loop->getDirection(indexCurve)); // compute linearization of NURBS curve

					int NoSampPoints = polylines.size()/2;

					int startIndex = 1;
					int endIndex = NoSampPoints;

					if(indexCurve == 0) // first curve in loop
						startIndex = 0;

					if(indexCurve == numCurves-1) // last curve in loop
						endIndex = NoSampPoints-1;

					// write points
					for(int i = startIndex; i < endIndex; i++){
						surfCoord[0] = polylines.at(2*i);
						surfCoord[1] = polylines.at(2*i+1);
						patch->computeCartesianCoordinates(cartCoord,surfCoord); // compute coord in cartesian coordinates
						WritePoint(myfile, cartCoord, pointCounter); // write point
					}

					// write line segments
					for(int i = 0; i < NoSampPoints-2; i++)
						WriteLineSegment(myfile, curveCounter, curveCounter, curveCounter+1);
					// write last segment
					if(indexCurve != numCurves-1) // check if curve is last curve in loop
						WriteLineSegment(myfile, curveCounter, curveCounter, curveCounter+1);
					else
						WriteLineSegment(myfile,curveCounter,curveCounter,firstLastPoint);

					myfile << endl;
				} // loop curve
			} // loop trimming

			// start of Patch declaration ------------------------------------------------------------------------------
			WriteSurfaceHeader(myfile, surfaceCounter); // write SurfaceHeader;

			// write number of curves per patch and the curve IDs
			int NoPatchCurves = curveCounter - curveCounterBeginning; // number of curves for patch
			myfile << NoPatchCurves << endl;

			for (int curveIndex = curveCounterBeginning; curveIndex < curveCounter; curveIndex++)
				myfile << curveIndex << SPACE; // write curve ID
			myfile << endl;

			// write orientations of curves
			for(int i=0;i<NoPatchCurves;i++)
				myfile << "0 ";
			myfile << endl;

			// write and compute approx center and corresponding normal (where label is drawn)
			// compute middle of knotspan in U and V => used as approx center
			double U_FirstKnot = patch->getIGABasis()->getUBSplineBasis1D()->getFirstKnot();
			double U_LastKnot = patch->getIGABasis()->getUBSplineBasis1D()->getLastKnot();

			double V_FirstKnot = patch->getIGABasis()->getVBSplineBasis1D()->getFirstKnot();
			double V_LastKnot = patch->getIGABasis()->getVBSplineBasis1D()->getLastKnot();

			double U_MidKnotSpan=(U_FirstKnot+U_LastKnot)/2;
			double V_MidKnotSpan=(V_FirstKnot+V_LastKnot)/2;

			double center[3];
			double normal[3];

			patch->computeCartesianCoordinatesAndNormalVector(center,normal,U_MidKnotSpan,V_MidKnotSpan);

			WriteCoordinates(myfile, center); // write center
			WriteCoordinates(myfile, normal); // write normal

			// write further information (N ofControlPoints u/v and pDegree u/v)
			int noCPU = patch->getUNoControlPoints();
			int noCPV = patch->getVNoControlPoints();

			int pDegreeU = patch->getIGABasis()->getUBSplineBasis1D()->getPolynomialDegree();
			int pDegreeV = patch->getIGABasis()->getVBSplineBasis1D()->getPolynomialDegree();

			myfile << "1 "; //1 stands for trimmed
			myfile << noCPU << SPACE << noCPV << SPACE << pDegreeU << SPACE << pDegreeV << endl;

			// compute and write Control Points of patch
			for (int indexCP = 0; indexCP < numCPSs; indexCP++) {
				// compute Point
				cartCoord[0] = patch->getControlPointNet()[indexCP]->getX();
				cartCoord[1] = patch->getControlPointNet()[indexCP]->getY();
				cartCoord[2] = patch->getControlPointNet()[indexCP]->getZ();

				WriteCoordinates(myfile, cartCoord); // write coordinates
			}

			// write knot vector
			// knot vector U
			int noKnots = patch->getIGABasis()->getUBSplineBasis1D()->getNoKnots();
			double* knotVector = patch->getIGABasis()->getUBSplineBasis1D()->getKnotVector();

			rescaleKnotVector(knotVector,noKnots);

			for (int indexKnot = 0; indexKnot < noKnots; indexKnot++)
				myfile << knotVector[indexKnot] << SPACE;
			myfile << endl;

			// knot vector V
			noKnots = patch->getIGABasis()->getVBSplineBasis1D()->getNoKnots();
			knotVector = patch->getIGABasis()->getVBSplineBasis1D()->getKnotVector();

			rescaleKnotVector(knotVector,noKnots);

			for (int indexKnot = 0; indexKnot < noKnots; indexKnot++)
				myfile << knotVector[indexKnot] << SPACE;
			myfile << endl;

			// write weights
			myfile << "1 "; // 1 stands for rational

			for (int indexCP = 0; indexCP < numCPSs; indexCP++)
				myfile << patch->getControlPointNet()[indexCP]->getW() << SPACE;
			patch->getControlPointNet()[1]->getW();
			myfile << endl<<endl ;

			// end of Patch declaration ------------------------------------------------------------------------------
		} // loop patch

		// ********** untrimmed patch **********
		else
			myfile << "untrimmed patch not yet implemented" << endl;

	} // loop patches

	myfile << "0"; // indicates the end of the definition of geometrical entities

	myfile.close();
}

void initDotPostRes(const std::string _fileName) {
	fstream dotPostResFile;
	dotPostResFile.open(_fileName.c_str(), ios_base::out);
	assert(!dotPostResFile.fail());
	dotPostResFile << headerDotPostRes;
	dotPostResFile.close();
}

void appendCPDataToDotRes(std::string _analysisName,std::string _meshName, std::string _dataFieldName, int _step, std::string _resultType, DataField* _dataField, std::map<int, std::vector<int> >& _patchToDof) {
	// open initialized .post.res-file to append data
	ofstream myfile;
	string fileName = _meshName + "_IGA.post.res";
	myfile.open(fileName.c_str(), ios::app);
	myfile.precision(14);
	myfile << std::dec;

	// set dimension of problem
	int dimension = 0;
	if (_resultType == "vector")
		dimension = 3;
	else if (_resultType == "scalar")
		dimension = 1;
	else{
		WARNING_OUT("!!! Dimension of Problem is incorrect !!!");
		assert(false);
	}

	// start writing data to file ------------------------------------------------------------------------------
	myfile << "Result " << "\"" << _dataFieldName << "\"" << SPACE << _analysisName << SPACE << _step << SPACE
			<< _resultType << SPACE << "OnNurbsSurface" << endl;
	myfile << "Values" << endl;

	// loop over patches
	for(map<int,vector<int> >::iterator itPatch=_patchToDof.begin(); itPatch!=_patchToDof.end(); itPatch++) {
		myfile << (itPatch->first)+1 << endl;
		INFO_OUT() << itPatch->second.size();
		for(vector<int>::iterator itCP=itPatch->second.begin();itCP!=itPatch->second.end();itCP++) {
			INFO_OUT()<<*itCP;
			for(int d=0;d<dimension;d++)
				myfile << _dataField->data[*itCP * dimension + d] << SPACE;
			myfile << endl;
		}
	}
	myfile << "End Values" << endl<<endl;
	// end writing data to file ------------------------------------------------------------------------------

	myfile.close();
}

void appendCPDataToDotRes(std::string _fileName, std::string _dataFieldName,std::string _analysisName, int _step, std::string _resultType, DataField* _dataField, const IGAMesh* const _mesh) {
	// build patch to dof id table
	// TODO should be done only once and reused, no recomputed
	int numPatches = _mesh->getNumPatches();
	std::map<int,std::vector<int> > patchToDof;
	for (int indexPatch = 0; indexPatch < numPatches; indexPatch++) {
		const EMPIRE::IGAPatchSurface* patch = _mesh->getSurfacePatch(indexPatch);
		for(int indexCP=0; indexCP < patch->getNoControlPoints(); indexCP++) {
			patchToDof[indexPatch].push_back((*patch)[indexCP]->getDofIndex());
		}
	}

	// open initialized .post.res-file to append data
	ofstream myfile;
	myfile.open(_fileName.c_str(), ios::app);
	myfile.precision(14);
	myfile << std::dec;

	// set dimension of problem
	int dimension = 0;
	if (_resultType == "vector")
		dimension = 3;
	else if (_resultType == "scalar")
		dimension = 1;
	else{
		WARNING_BLOCK_OUT("GidIFAFileIO","appendCPDataToDotRes","Dimension of datafield not available for output");
		return;
	}

	// start writing data to file ------------------------------------------------------------------------------
	myfile << "Result " << "\"" << _dataFieldName << "\"" << SPACE << _analysisName << SPACE << _step << SPACE
			<< _resultType << SPACE << "OnNurbsSurface" << endl;
	myfile << "Values" << endl;

	// loop over patches
	for(map<int,vector<int> >::iterator itPatch = patchToDof.begin(); itPatch != patchToDof.end(); itPatch++) {
		myfile << (itPatch->first)+1 << endl;
		for(vector<int>::iterator itCP = itPatch->second.begin();itCP != itPatch->second.end();itCP++) {
			for(int d=0;d<dimension;d++)
				myfile << _dataField->data[*itCP * dimension + d] << SPACE;
			myfile << endl;
		}
	}
	myfile << "End Values" << endl<<endl;
	// end writing data to file ------------------------------------------------------------------------------

	myfile.close();
}

void linearizeUsingNCPxP(std::vector<double>& polylines, const EMPIRE::IGAPatchCurve* curve, bool curveDirection) {
	polylines.clear();
	int nCP = curve->getNoControlPoints();
	int p = curve->getIGABasis()->getPolynomialDegree();
	int factor = fmax(0,4-p);  //4 is arbitrary degree upon linearization is fully NCPxP
	factor = 1 + factor*factor*factor;
	double u0 = curve->getIGABasis()->getFirstKnot();
	double u1 = curve->getIGABasis()->getLastKnot();
	double du = (u1-u0)/(nCP*p*factor-1);
	/// Check direction to put points in the right sequence (counter clockwise for outter loop, clockwise for inner
	if(curveDirection) {
		for(int i=0;i<nCP*p*factor;i++) {
			double knot = u0 + i*du;
			double parametricCoordinates[2] = {0};
			curve->computeCartesianCoordinates(parametricCoordinates,knot);
			polylines.push_back(parametricCoordinates[0]);
			polylines.push_back(parametricCoordinates[1]);
		}
	}
	else {
		for(int i=nCP*p*factor-1;i>=0;i--) {
			double knot = u0 + i*du;
			double parametricCoordinates[2] = {0};
			curve->computeCartesianCoordinates(parametricCoordinates,knot);
			polylines.push_back(parametricCoordinates[0]);
			polylines.push_back(parametricCoordinates[1]);
		}
	}
}

void rescaleKnotVector(double* _knotVector, int _noKnots, double _firstKnot, double _lastKnot){
	if(_firstKnot!=0)
		WARNING_OUT("rescaling to non-zero start of span not yet implemented");

	double shift = -_knotVector[0];

	for(int i=0;i<_noKnots;i++)
		_knotVector[i] += shift;

	double scaling = _lastKnot/_knotVector[_noKnots-1];

	for(int i=0;i<_noKnots;i++)
		_knotVector[i] *= scaling;

	/*test to rescale to arbirtary span => doesn't work because of multiplicity!
	double spacing = fabs((firstKnot-lastKnot)/(noKnots-1));

	knotVector[0] = firstKnot;

	for(int i=1;i<noKnots;i++)
		knotVector[i] = knotVector[0] + spacing * i;*/
}

void WriteCoordinates(std::ostream& myfile, double* _coords){
	myfile << _coords[0] << SPACE << _coords[1] << SPACE << _coords[2] << endl;
}

void WritePoint(std::ostream& myfile, double* _coords, unsigned int &pointCounter){
	myfile << "1 " << pointCounter << " 0 0 2 0 0 2 0" << endl;	// write point header
	pointCounter++;
	WriteCoordinates(myfile, _coords); // write point coordinates
	myfile << endl;
}

void WriteLineSegment(std::ostream& myfile, unsigned int &curveCounter, int startPoint, int endPoint){
	myfile << "2 " << curveCounter << " 0 0 1 0 0 2 0" << endl;// write segment header
	myfile << startPoint << SPACE << endPoint << endl;	// write points that define segment
	curveCounter++;
}

void WriteSurfaceHeader(std::ostream& myfile, unsigned int &surfaceCounter){
	myfile << "14 " << surfaceCounter << " 0 0 0 0 0 2 0" << endl; // write surface header
	surfaceCounter++;
}

} /* namespace GiDIGAFileIO */

} /* namespace EMPIRE */

