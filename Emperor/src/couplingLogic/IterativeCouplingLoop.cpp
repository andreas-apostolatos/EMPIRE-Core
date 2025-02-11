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
#include <assert.h>
#include <iostream>
#include <sstream>

#include "IterativeCouplingLoop.h"
#include "ConvergenceChecker.h"
#include "DataField.h"
#include "ClientCode.h"
#include "AbstractCouplingAlgorithm.h"
#include "Message.h"
#include "DataOutput.h"

using namespace std;

namespace EMPIRE {

IterativeCouplingLoop::IterativeCouplingLoop() :
		AbstractCouplingLogic(), convergenceObserverVec(), convergenceChecker(
				NULL) {
	outputCounter = 0;
}

IterativeCouplingLoop::~IterativeCouplingLoop() {
	delete convergenceChecker;
}

void IterativeCouplingLoop::setConvergenceChecker(
		ConvergenceChecker *_convergenceChecker) {
	assert(_convergenceChecker != NULL);
	assert(convergenceChecker == NULL);
	convergenceChecker = _convergenceChecker;
}

void IterativeCouplingLoop::doCoupling() {
	assert(convergenceChecker != NULL);
	int count = 0;
	// notify the coupling algorithms the start of a new time step
	for (int i = 0; i < couplingAlgorithmVec.size(); i++) {
		couplingAlgorithmVec[i]->setNewTimeStep();
	}

	// initialize output files
	outputCounter++;
	stringstream rearPart;
	rearPart << "_" << outputCounter;
	for (int i = 0; i < dataOutputVec.size(); i++)
		dataOutputVec[i]->init(rearPart.str());

	while (true) {
		count++;
		stringstream ss;
		ss << "iteration step: " << count;
		HEADING_OUT(4, "IterativeCouplingLoop", ss.str(), infoOut);
		// update data in coupling algorithm
		for (int i = 0; i < couplingAlgorithmVec.size(); i++) {
			couplingAlgorithmVec[i]->updateAtIterationBeginning();
			// This is just for the first iteration.
			// To make iteration end value not zero.
			if(count == 1){
				// update data in coupling algorithm
				for (int i = 0; i < couplingAlgorithmVec.size(); i++) {
					couplingAlgorithmVec[i]->updateAtIterationEnd();
				}
			}
			couplingAlgorithmVec[i]->setCurrentIteration(count);
			couplingAlgorithmVec[i]->setCurrentTimeStep(outputCounter);
		}

		// do coupling
		for (int i = 0; i < couplingLogicSequence.size(); i++)
			couplingLogicSequence[i]->doCoupling();

		// write data field at this iteration
		for (int i = 0; i < dataOutputVec.size(); i++)
			dataOutputVec[i]->writeCurrentStep(count);

		// update data in coupling algorithm
		for (int i = 0; i < couplingAlgorithmVec.size(); i++) {
			couplingAlgorithmVec[i]->updateAtIterationEnd();
		}

		// compute the new residual for the coupling algorithm.
		for (int i = 0; i < couplingAlgorithmVec.size(); i++) {
			couplingAlgorithmVec[i]->calcCurrentResidual();
		}

		// broadcast convergence
		if (convergenceChecker->isConvergent()) {
			broadcastConvergenceToClients(true);
			break;
		} else {
			broadcastConvergenceToClients(false);
		}

		// compute the new output of the coupling algorithm
		for (int i = 0; i < couplingAlgorithmVec.size(); i++) {
			couplingAlgorithmVec[i]->calcNewValue();
		}

		assert(count == convergenceChecker->getCurrentNumOfIterations());
	}
	//std::cout << "number of iterative coupling loops: " << count << std::endl;
}

void IterativeCouplingLoop::addConvergenceObserver(ClientCode *clientCode) {
	convergenceObserverVec.push_back(clientCode);
}

void IterativeCouplingLoop::broadcastConvergenceToClients(bool convergent) {
	for (int i = 0; i < convergenceObserverVec.size(); i++) {
		convergenceObserverVec[i]->sendConvergenceSignal(convergent);
	}
}

void IterativeCouplingLoop::addCouplingAlgorithm(
		AbstractCouplingAlgorithm *_couplingAlgorithm) {
	couplingAlgorithmVec.push_back(_couplingAlgorithm);
}

} /* namespace EMPIRE */
