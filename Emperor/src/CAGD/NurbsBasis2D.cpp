/*  Copyright &copy; 2013, TU Muenchen, Chair of Structural Analysis,
 *  Stefan Sicklinger, Tianyang Wang, Andreas Apostolatos, Munich
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
// Inclusion of standard libraries
#include <iostream>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

// Inclusion of user defined libraries
#include "NurbsBasis2D.h"
//#include "IGAMath.h"
// Edit Aditya
#include "MathLibrary.h"
#include "Message.h"
#define GETINDEX(x,y) (x*49+y)

using namespace std;

namespace EMPIRE {

NurbsBasis2D::NurbsBasis2D(int _ID = 0, int _pDegree = 0, int _noKnotsU = 0, double* _KnotVectorU =
        NULL, int _qDegree = 0, int _noKnotsV = 0, double* _KnotVectorV = NULL,
        int _uNoBasisFnc = 0, int _vNoBasisFnc = 0, double* _igaControlPointWeights = NULL) :
        BSplineBasis2D(_ID, _pDegree, _noKnotsU, _KnotVectorU, _qDegree, _noKnotsV, _KnotVectorV), uNoBasisFnc(
                _uNoBasisFnc), vNoBasisFnc(_vNoBasisFnc) {

    // Read input
    bool ucondition = uNoBasisFnc
            != uBSplineBasis1D->getNoKnots() - uBSplineBasis1D->getPolynomialDegree() - 1;
    bool vcondition = vNoBasisFnc
            != vBSplineBasis1D->getNoKnots() - vBSplineBasis1D->getPolynomialDegree() - 1;
    if (ucondition || vcondition) {
        ERROR_OUT() << "Error in NurbsBasis2D::NurbsBasis2D" << endl;
        ERROR_OUT() << "The number of Control Points, the polynomial degrees and the knot vectors do not match" << endl;
        exit(-1);
    }

    // Assign the pointer of the Control Point Weights to the protected member of the class
    IGAControlPointWeights = _igaControlPointWeights;
}

NurbsBasis2D::~NurbsBasis2D() {
    delete[] IGAControlPointWeights;
}

NurbsBasis2D::NurbsBasis2D(const NurbsBasis2D& _nurbsBasis2D) :
        BSplineBasis2D(_nurbsBasis2D) {

    // Compute the number of the Control Points at each direction
    uNoBasisFnc = _nurbsBasis2D.uNoBasisFnc;
    vNoBasisFnc = _nurbsBasis2D.vNoBasisFnc;

    // Allocate memory for the Control Point weights
    IGAControlPointWeights = new double[uNoBasisFnc * vNoBasisFnc];
    for (int i = 0; i < uNoBasisFnc * vNoBasisFnc; i++)
        IGAControlPointWeights[i] = _nurbsBasis2D.IGAControlPointWeights[i];
}

void NurbsBasis2D::computeLocalBasisFunctions(double* _basisFcts, double _uPrm, int _KnotSpanIndexU,
        double _vPrm, int _KnotSpanIndexV) {
    /*
     *  Computes the NURBS basis functions in 2D
     *
     *  eta
     *   |
     *   |	CP(1,m) --> R((m-1)*n+1)	CP(2,m) --> R((m-1)*n+2)	...		CP(n,m) --> R(n*m)
     *   |
     *   |	...							...							...	 	...
     *   |
     *   |	CP(1,2) --> R(n+1)			CP(2,2) --> R(n+2)			...		CP(n,2) --> R(2*n)
     *   |
     *   |	CP(1,1) --> R(1)			CP(2,1) --> R(2)			...		CP(n,1) --> R(n)
     *   |______________________________________________________________________________________xi
     *
     *   The functional values are sorted in a vector as follows:
     *
     *  _basisFcts = [R(1)	R(2)	...		R(n)	R(n+1)	R(n+2)	...		R(2*n)	...		R((m-1)*n+1)	R((m-1)*n+2)	...		R(n*m)]
     *
     *  Assuming that there are n basis functions in u-direction and m basis functions in v-direction
     *
     */

    // Read input
    assert(_basisFcts!=NULL);

    // Compute the B-Spline basis functions at each surface parameter
    double* uBSplinebasis1DFcts = new double[uBSplineBasis1D->getPolynomialDegree() + 1];
    uBSplineBasis1D->computeLocalBasisFunctions(uBSplinebasis1DFcts, _uPrm, _KnotSpanIndexU);

    // Compute the local B-Spline basis functions in v-direction
    double* vBSplinebasis1DFcts = new double[vBSplineBasis1D->getPolynomialDegree() + 1];
    vBSplineBasis1D->computeLocalBasisFunctions(vBSplinebasis1DFcts, _vPrm, _KnotSpanIndexV);

    int noLocalBasisFunctions = (uBSplineBasis1D->getPolynomialDegree() + 1)
            * (vBSplineBasis1D->getPolynomialDegree() + 1);

    // Initialize auxiliary variables
    int uIndexCP = 0;
    int vIndexCP = 0;

    // Initialize partial sum
    double sum = 0.0;

    // Initialize counter
    int counter = 0;

    // Sum up all the contributions at the knot span of interest, avoid additions with zeros for efficiency
    for (int j = 0; j <= vBSplineBasis1D->getPolynomialDegree(); j++) {
        for (int i = 0; i <= uBSplineBasis1D->getPolynomialDegree(); i++) {

            // Get the indices of the Control Points affected at the current knot span location
            uIndexCP = _KnotSpanIndexU - uBSplineBasis1D->getPolynomialDegree() + i;
            vIndexCP = _KnotSpanIndexV - vBSplineBasis1D->getPolynomialDegree() + j;

            // Recursively compute the basis functions
            _basisFcts[counter] = uBSplinebasis1DFcts[i] * vBSplinebasis1DFcts[j]
                    * IGAControlPointWeights[vIndexCP * uNoBasisFnc + uIndexCP];

            // Update the partial sum
            sum += _basisFcts[counter];

            // Update counter
            counter += 1;
        }
    }

    // Divide through by sum to obtain the final expression
    for (int i = 0; i < noLocalBasisFunctions; i++) {
        _basisFcts[i] /= sum;
    }

    // Clear the memory on the heap from the pointers
    delete[] uBSplinebasis1DFcts;
    delete[] vBSplinebasis1DFcts;
}

void NurbsBasis2D::computeDenominatorFunctionAndDerivatives(double* _denominatorFctAndDerivs,
        double* _bSplineBasisFctsAndDerivs, int _derivDegree, int _KnotSpanIndexU,
        int _KnotSpanIndexV) {
    /*
     *  Computes the denominator function w(u,v) = Sum_(i=1)^n N_i(u,v) * w_i and its derivatives given the B-Spline basis functions N_i and their
     *  derivatives at (u,v). This function is needed for the computation of the derivatives to the NURBS basis functions.
     *
     *  Sorting idea for the 2D NURBS basis functions:
     *
     *  eta
     *   |
     *   |  CP(1,m) --> (m-1)*n+1   CP(2,m) --> (m-1)*n+2   ...     CP(n,m) --> n*m
     *   |
     *   |  ...                         ...                 ...     ...
     *   |
     *   |  CP(1,2) --> n+1         CP(2,2) --> n+2         ...     CP(n,2) --> 2*n
     *   |
     *   |  CP(1,1) --> 1           CP(2,1) --> 2           ...     CP(n,1) --> n
     *   |_______________________________________________________________________________xi
     *
     *  On the input/output array _denominatorFctAndDerivs:
     *  __________________________________________________
     *
     *  · The functional values are stored in a 2 dimensional array which is sorted in an 1D pointer array:
     *    _denominatorFctAndDerivs = new double[(_derivDegree + 1) * (_derivDegree + 2) / 2]
     *    computing all the partial derivatives in u-direction k-th and in v-direction l-th as 0 <= k + l <= _derivDegree
     *
     *  · The element _bSplineBasisFctsAndDerivs[index] where index = indexDerivativeBasisFunction(_derivDegree,i,j,k) of the output array
     *   returns the i-th derivative with respect to u and the j-th derivative with respect to v (in total i+j-th partial derivative)
     *   of the k-th basis B-Spline basis function N_(k) sorted as in the above table
     *
     *  Reference: Piegl, Les and Tiller, Wayne, The NURBS Book. Springer-Verlag: Berlin 1995; p. 137.
     *  _________
     *
     *  It is also assumed that there are n basis functions in u-direction and m basis functions in v-direction.
     */

    // Read input
    assert(_denominatorFctAndDerivs!=NULL);
    assert(_bSplineBasisFctsAndDerivs!=NULL);

    // Get the polynomial degrees
    int pDegree = getUBSplineBasis1D()->getPolynomialDegree();
    int qDegree = getVBSplineBasis1D()->getPolynomialDegree();

    // The number of the basis functions
    int noBasisFcts = (pDegree + 1) * (qDegree + 1);

    // Initialize the output array to 0.0 exploiting only the entries which will be used later on according to the rule 0 <= k + l <= _derivDegree
    for (int i = 0; i <= _derivDegree; i++) {
        for (int j = 0; j <= _derivDegree - i; j++) {
            // Set the initial value to zero
            _denominatorFctAndDerivs[j * (_derivDegree + 1) + i] = 0.0;
        }
    }

    // Initialize the index of the basis function and Control Point weights
    int indexBasis = 0;
    int uIndexCP = 0;
    int vIndexCP = 0;

    // Initialize the counter of the basis functions
    int counterBasis = 0;

    // Loop over all the partial derivatives with respect to u-direction
    for (int j = 0; j <= _derivDegree; j++) {
        // Loop over all the partial derivatives with respect to v-direction
        for (int i = 0; i <= _derivDegree - j; i++) {
            // Loop over all the contributions from each basis function in v-direction
            for (int l = 0; l <= qDegree; l++) {
                // Loop over all the contributions from each basis function in u-direction
                for (int k = 0; k <= pDegree; k++) {
                    // Compute the basis function index
                    indexBasis = this->indexDerivativeBasisFunction(_derivDegree, j, i,
                            counterBasis);

                    // Compute the Control Point indices
                    uIndexCP = _KnotSpanIndexU - pDegree + k;
                    vIndexCP = _KnotSpanIndexV - qDegree + l;

                    // Sum up the contribution to
                    _denominatorFctAndDerivs[i * (_derivDegree + 1) + j] +=
                            _bSplineBasisFctsAndDerivs[indexBasis]
                                    * IGAControlPointWeights[vIndexCP * uNoBasisFnc + uIndexCP];

                    // Update the counter of the basis functions
                    counterBasis++;
                }
            }
            // Reset the counter of the basis functions
            counterBasis = 0;
        }
    }
}

void NurbsBasis2D::computeLocalBasisFunctionsAndDerivatives(double* _basisFctsAndDerivs,
        int _derivDegree, double _uPrm, int _KnotSpanIndexU, double _vPrm, int _KnotSpanIndexV) {
    /*
     *  Computes the NURBS basis functions and their derivatives at (_uPrm,_vPrm) surface parameters and stores them into the
     *  input/output array _basisFctsAndDerivs.
     *
     *  Sorting idea for the 2D NURBS basis functions:
     *
     *  eta
     *   |
     *   |  CP(1,m) --> (m-1)*n+1   CP(2,m) --> (m-1)*n+2   ...     CP(n,m) --> n*m
     *   |
     *   |  ...                         ...                 ...     ...
     *   |
     *   |  CP(1,2) --> n+1         CP(2,2) --> n+2         ...     CP(n,2) --> 2*n
     *   |
     *   |  CP(1,1) --> 1           CP(2,1) --> 2           ...     CP(n,1) --> n
     *   |_______________________________________________________________________________xi
     *
     * On the input/output array _basisFctsAndDerivs:
     * _____________________________________________
     *
     * · The functional values are sorted in a 3 dimensional array which is in turned sorted in an 1D pointer array:
     *   _basisFctsAndDerivs = new double[(_derivDegree + 1) * (_derivDegree + 1) * noBasisFcts]
     *   computing all the partial derivatives in u-direction k-th and in v-direction l-th as 0 <= k + l <= _derivDegree
     *
     *  · The element _bSplineBasisFctsAndDerivs[index] where index = indexDerivativeBasisFunction(_derivDegree,i,j,k) of the output array
     *   returns the i-th derivative with respect to u and the j-th derivative with respect to v (in total i+j-th partial derivative)
     *   of the k-th basis B-Spline basis function N_(k) sorted as in the above table
     *
     *  Reference: Piegl, Les and Tiller, Wayne, The NURBS Book. Springer-Verlag: Berlin 1995; p. 137.
     *  _________
     *
     *  It is also assumed that there are n basis functions in u-direction and m basis functions in v-direction.
     */

    // Read input
    assert(_basisFctsAndDerivs!=NULL);

    // Get the polynomial degrees
    int pDegree = getUBSplineBasis1D()->getPolynomialDegree();
    int qDegree = getVBSplineBasis1D()->getPolynomialDegree();

    // The number of the basis functions
    int noBasisFcts = (pDegree + 1) * (qDegree + 1);

    // Initialize the index of the basis
    int indexBSplineBasis = 0;
    int indexNurbsBasis = 0;

    // Initialize the output array to zero
    for (int i = 0; i <= _derivDegree; i++)
        for (int j = 0; j <= _derivDegree - i; j++)
            for (int k = 0; k < noBasisFcts; k++) {
                // Compute the index of the basis
                indexNurbsBasis = indexDerivativeBasisFunction(_derivDegree, i, j, k);

                // Initialize the B-Spline basis function value
                _basisFctsAndDerivs[indexNurbsBasis] = 0.0;
            }

    // Compute the B-Spline basis functions and their partial derivatives up to _derivDegree absolute order
    double* bSplineBasisFctAndDeriv = new double[(_derivDegree + 1) * (_derivDegree + 2)
            * noBasisFcts / 2];
    BSplineBasis2D::computeLocalBasisFunctionsAndDerivatives(bSplineBasisFctAndDeriv, _derivDegree,
            _uPrm, _KnotSpanIndexU, _vPrm, _KnotSpanIndexV);

    // Initialize the counter of the basis functions
    int counterBasis = 0;

    // Initialize the index the Control Point weights
    int uIndexCP = 0;
    int vIndexCP = 0;

    // Compute the denominator function
    double* denominatorFct = new double[(_derivDegree + 1) * (_derivDegree + 1)];
    computeDenominatorFunctionAndDerivatives(denominatorFct, bSplineBasisFctAndDeriv, _derivDegree,
            _KnotSpanIndexU, _KnotSpanIndexV);

    // Initialize auxiliary variables
    double v = 0.0;
    double v2 = 0.0;

    // Reset basis function counter to zero
    counterBasis = 0;

    // Loop over all the basis functions in v-direction
    for (int vBasis = 0; vBasis <= qDegree; vBasis++) {
        // Loop over all the basis functions in u-direction
        for (int uBasis = 0; uBasis <= pDegree; uBasis++) {
            // Loop over all the derivatives in u-direction
            for (int k = 0; k <= _derivDegree; k++) {
                // Loop over all the derivatives in v-direction
                for (int l = 0; l <= _derivDegree - k; l++) {
                    // Compute the B-Spline basis function index
                    indexBSplineBasis = indexDerivativeBasisFunction(_derivDegree, k, l,
                            counterBasis);

                    // Compute the Control Point indices
                    uIndexCP = _KnotSpanIndexU - pDegree + uBasis;
                    vIndexCP = _KnotSpanIndexV - qDegree + vBasis;

                    // Store temporary value
                    v = bSplineBasisFctAndDeriv[indexBSplineBasis]
                            * IGAControlPointWeights[vIndexCP * uNoBasisFnc + uIndexCP];

                    for (int j = 1; j <= l; j++) {
                        // Compute the NURBS basis function index
                        indexNurbsBasis = indexDerivativeBasisFunction(_derivDegree, k, l - j,
                                counterBasis);

                        // Update the scalar value
                        v -= EMPIRE::MathLibrary::binomialCoefficients[GETINDEX(l, j)]
                                * denominatorFct[j * (_derivDegree + 1)]
                                * _basisFctsAndDerivs[indexNurbsBasis];
                    }

                    for (int i = 1; i <= k; i++) {
                        // Compute the NURBS basis function index
                        indexNurbsBasis = indexDerivativeBasisFunction(_derivDegree, k - i, l,
                                counterBasis);

                        // Update the scalar value
                        v -= EMPIRE::MathLibrary::binomialCoefficients[GETINDEX(k, i)]
                                * denominatorFct[i] * _basisFctsAndDerivs[indexNurbsBasis];
                        v2 = 0.0;

                        for (int j = 1; j <= l; j++) {
                            // Compute the NURBS basis function index
                            indexNurbsBasis = indexDerivativeBasisFunction(_derivDegree, k - i,
                                    l - j, counterBasis);

                            // Update the scalar value
                            v2 += EMPIRE::MathLibrary::binomialCoefficients[GETINDEX(l, j)]
                                    * denominatorFct[j * (_derivDegree + 1) + i]
                                    * _basisFctsAndDerivs[indexNurbsBasis];
                        }
                        v -= EMPIRE::MathLibrary::binomialCoefficients[GETINDEX(k, i)] * v2;
                    }
                    // Compute the NURBS basis function index
                    indexNurbsBasis = indexDerivativeBasisFunction(_derivDegree, k, l,
                            counterBasis);

                    // Update the value of the basis function derivatives
                    _basisFctsAndDerivs[indexNurbsBasis] = v / denominatorFct[0];
                }
            }
            // Update basis function's counter
            counterBasis++;
        }
    }

    // Free the memory from the heap
    delete[] bSplineBasisFctAndDeriv;
    delete[] denominatorFct;
}

Message &operator<<(Message &message, NurbsBasis2D &nurbsBasis2D) {
    message << "\t" << "NurbsBasis2D: " << endl;

    message << "\t\tpDegree:  " << nurbsBasis2D.getUBSplineBasis1D()->getPolynomialDegree() << endl;
    message << "\t\tqDegree:  " << nurbsBasis2D.getVBSplineBasis1D()->getPolynomialDegree() << endl;

    message << "\t\tKnots Vector U: [\t";
    for (int i = 0; i < nurbsBasis2D.getUBSplineBasis1D()->getNoKnots(); i++)
        message << nurbsBasis2D.getUBSplineBasis1D()->getKnotVector()[i] << "\t";
    message << "]" << endl;

    message << "\t\tKnots Vector V: [\t";
    for (int i = 0; i < nurbsBasis2D.getVBSplineBasis1D()->getNoKnots(); i++)
        message << nurbsBasis2D.getVBSplineBasis1D()->getKnotVector()[i] << "\t";
    message << "]" << endl;

    message << "\t\tControl Points Net: " << endl;
    int count = 0;
    for (int j = 0; j < nurbsBasis2D.getVNoBasisFnc(); j++) {
        ERROR_OUT() << "\t\t";
        for (int i = 0; i < nurbsBasis2D.getUNoBasisFnc(); i++) {
            int Index = j * nurbsBasis2D.getUNoBasisFnc() + i;
            message << nurbsBasis2D.getIGAControlPointWeights()[Index] << "\t";
            count++;
        }
        message << endl;
    }

    message() << "\t" << "---------------------------------" << endl;
    return message;
}


}/* namespace EMPIRE */
