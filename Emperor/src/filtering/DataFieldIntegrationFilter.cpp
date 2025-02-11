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
#ifdef USE_INTEL_MKL
#include <mkl.h>
#include <mkl_lapacke.h>
#include <mkl_spblas.h>
#endif

#include "DataFieldIntegrationFilter.h"
#include "DataFieldIntegrationAdapter.h"
#include "ConnectionIO.h"
#include "DataField.h"

using namespace std;

namespace EMPIRE {

DataFieldIntegrationFilter::DataFieldIntegrationFilter(AbstractMesh *_mesh):
	mesh(_mesh) {
	dataFieldIntegrationAdapter = new DataFieldIntegrationAdapter(_mesh);
}

DataFieldIntegrationFilter::~DataFieldIntegrationFilter() {
    delete dataFieldIntegrationAdapter;
}

void DataFieldIntegrationFilter::filtering() {
    if (doIntegration)
        integrate();
    else
        deIntegrate();
}

void DataFieldIntegrationFilter::init() {
    assert(inputVec.size() == 1);
    assert(outputVec.size() == 1);
    assert(inputVec[0]->type == outputVec[0]->type);
    assert(inputVec[0]->type == EMPIRE_ConnectionIO_DataField);

    assert(inputVec[0]->mesh == outputVec[0]->mesh);
    assert(inputVec[0]->mesh == mesh);

    DataField *inDataField = inputVec[0]->dataField;
    DataField *outDataField = outputVec[0]->dataField;
    assert(inDataField->dimension == outDataField->dimension);
    assert(inDataField->location == outDataField->location);
    assert(inDataField->location == EMPIRE_DataField_atNode);
    assert(inDataField->dimension != EMPIRE_DataField_doubleVector);
    assert(outDataField->dimension != EMPIRE_DataField_doubleVector);
    if (inDataField->typeOfQuantity == EMPIRE_DataField_field
            && outDataField->typeOfQuantity == EMPIRE_DataField_fieldIntegral) {
        doIntegration = true;
    } else if (inDataField->typeOfQuantity == EMPIRE_DataField_fieldIntegral
            && outDataField->typeOfQuantity == EMPIRE_DataField_field) {
        doIntegration = false;
    } else {
        assert(false);
    }
}

void DataFieldIntegrationFilter::integrate() {
    DataField *inDataField = inputVec[0]->dataField;
    DataField *outDataField = outputVec[0]->dataField;
    int n = inDataField->numLocations;
    double *inDataFieldDOFi = new double[n];
    double *outDataFieldDOFi = new double[n];
    int numDOFs = inDataField->dimension;
    for (int i = 0; i < numDOFs; i++) {
        for (int j = 0; j < inDataField->numLocations; j++)
            inDataFieldDOFi[j] = inDataField->data[j * numDOFs + i];
        dataFieldIntegrationAdapter->integrate(inDataFieldDOFi, outDataFieldDOFi);
        for (int j = 0; j < n; j++)
            outDataField->data[j * numDOFs + i] = outDataFieldDOFi[j];
    }
    delete[] inDataFieldDOFi;
    delete[] outDataFieldDOFi;
}

void DataFieldIntegrationFilter::deIntegrate() {
    DataField *inDataField = inputVec[0]->dataField;
    DataField *outDataField = outputVec[0]->dataField;
    int n = inDataField->numLocations;
    double *inDataFieldDOFi = new double[n];
    double *outDataFieldDOFi = new double[n];
    int numDOFs = inDataField->dimension;
    for (int i = 0; i < numDOFs; i++) {
        for (int j = 0; j < inDataField->numLocations; j++)
            inDataFieldDOFi[j] = inDataField->data[j * numDOFs + i];
        dataFieldIntegrationAdapter->deIntegrate(inDataFieldDOFi, outDataFieldDOFi);
        for (int j = 0; j < n; j++)
            outDataField->data[j * numDOFs + i] = outDataFieldDOFi[j];
    }
    delete[] inDataFieldDOFi;
    delete[] outDataFieldDOFi;
}
} /* namespace EMPIRE */
