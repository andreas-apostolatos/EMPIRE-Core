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
#include "ConstantRelaxation.h"
#include "DataField.h"
#include "ConnectionIO.h"
#include "Signal.h"
#include "Residual.h"

#include <assert.h>
#include <math.h>
#include <sstream>
//#include <fstream>
//#include <iomanip>

using namespace std;

namespace EMPIRE {

ConstantRelaxation::ConstantRelaxation(std::string _name, double _relaxationFactor) :
        AbstractCouplingAlgorithm(_name), RELAXATION_FACTOR(_relaxationFactor) {
    ///assert(RELAXATION_FACTOR>0.0); can be also negative
    debugMe = false;
}

ConstantRelaxation::~ConstantRelaxation() {
}

void ConstantRelaxation::calcNewValue() {


    // calculate the new output
    assert(outputs.size() == residuals.size());
    for (map<int, Residual*>::iterator it = residuals.begin(); it != residuals.end(); it++) {
        Residual *residual = it->second;
        assert(outputs.find(it->first) != outputs.end());
        CouplingAlgorithmOutput *output = outputs.find(it->first)->second;

        assert(residual->size == output->size);
        double *newOuput = new double[residual->size];
        // U_i_n+1 = U_i_n + alpha R_i_n
        for (int i=0; i<residual->size; i++) {
            newOuput[i] = output->outputCopyAtIterationBeginning[i]+ RELAXATION_FACTOR*residual->residualVector[i] ;
        }
        output->overwrite(newOuput);
        delete[] newOuput;

    }
}

void ConstantRelaxation::calcCurrentResidual() {
    // compute the current residual
    for (map<int, Residual*>::iterator it = residuals.begin(); it != residuals.end(); it++) {
        it->second->computeCurrentResidual();
    }
}

//
//// Writing out the ouputCopyAtIterationBeginning
//std::ofstream file;
//char fileName[22];
//int n = sprintf(fileName,"outputCopyAtIterationBeginning_%d_%d",currentTimeStep,currentIteration);
//file.open (fileName);
//for (int i = 0; i < output->size; i++) {
//    file << output->outputCopyAtIterationBeginning[i+0]<<" "<<output->outputCopyAtIterationBeginning[i+1]<<" "<<output->outputCopyAtIterationBeginning[i+2]<<"\n";
//}
//
//
//// Writing out the ouputCopyAtIterationBeginning
//std::ofstream testfile;
//char testfileName[22];
//int in = sprintf(testfileName,"residualVector_%d_%d",currentTimeStep,currentIteration);
//testfile.open (testfileName);
//for (int i = 0; i < output->size; i++) {
//	testfile << residual->residualVector[i+0]<<" "<<residual->residualVector[i+1]<<" "<<residual->residualVector[i+2]<<"\n";
//}



} /* namespace EMPIRE */
