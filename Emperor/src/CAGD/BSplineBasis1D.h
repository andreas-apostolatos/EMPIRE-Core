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
/***********************************************************************************************//**
* \file NurbsBasis1D.h
 * This file holds the class NurbsBasis1D.h
 * \date 11/3/2013
 **************************************************************************************************/

#ifndef BSPLINEBASIS1D_H_
#define BSPLINEBASIS1D_H_

// Inclusion of user defined libraries
#include "AbstractBSplineBasis1D.h"

namespace EMPIRE {
class Message;

/********//**
 * \brief class BSplineBasis1D is related to the BSpline basis functions in 1D case
 ***********/

class BSplineBasis1D: public AbstractBSplineBasis1D {

protected:
    /// The polynomial degree of the basis
    int PDegree;

    /// The number of knots
    int NoKnots;

    /// The knot vector
    double* KnotVector;

    /// The constructor, the destructor, the copy constructor and the copy assignment
public:
    /***********************************************************************************************
     * \brief Constructor
     * \param[in] _ID The id of the basis
     * \param[in] _pDegree The polynomial degree of the basis
     * \param[in] _noKnots The number of knots
     * \param[in] _KnotVector The knot vector
     * \author Andreas Apostolatos
     ***********/
    BSplineBasis1D(int, int, int, double*);

    /***********************************************************************************************
     * \brief Destructor
     * \author Andreas Apostolatos
     ***********/
    ~BSplineBasis1D();

    /***********************************************************************************************
     * \brief Copy constructor
     * \author Andreas Apostolatos
     ***********/
    BSplineBasis1D(const BSplineBasis1D&);

    /***********************************************************************************************
     * \brief Copy assignment
     * \author Andreas Apostolatos
     ***********/
    BSplineBasis1D& operator=(const BSplineBasis1D&);

    /// On the 1D B-Spline basis functions
public:
    /***********************************************************************************************
     * \brief Returns the number of basis functions
     * \author Andreas Apostolatos
     ***********/
    inline int computeNoBasisFunctions() const {
        return NoKnots - PDegree - 1;
    }

    /***********************************************************************************************
     * \brief Compute Greville abscissae for the control point k.
     * The Greville abscissae is the knot where the control point k has the most influence.
     * Each related basis function is close to its maximum at the Greville abscissae
     * \param[in] cp The index of the control point in the local nurbs curve (starts from 0)
     * \return k The knot value corresponding to the Greville abscissae
     * \author Fabien Pean
     ***********/
    double computeGrevilleAbscissae(const int _controlPointIndex) const;

    /***********************************************************************************************
     * \brief Clamps the parameter to the Knot vector bounds
     * \param[in/out] _uPrm The parameter to clamp
     * \return bool Gives 1 if the input knot was inside the Knot vector or close enough, 0 if outside
     * \author Fabien Pean
     ***********/
    bool clampKnot(double&, double _tol = EPS_ACCPETEDINTOKNOTSPAN) const;

    /***********************************************************************************************
     * \brief Returns the polynomial degree of the B-Spline 1D basis
     * \param[in] _uPrm The parameter on which the knot span is searched
     * \author Andreas Apostolatos
     ***********/
    int findKnotSpan(double) const;

    /***********************************************************************************************
     * \brief Compute the non-zero B-Spline basis functions at the given parameter
     * \param[in/out] _basisFcts The non-zero basis functions at the given parameter
     * \param[in] _uPrm The parameter where the basis functions are evaluated
     * \param[in] _KnotSpanIndex The index of the knot span where _uPrm lives in
     * \author Andreas Apostolatos
     ***********/
    void computeLocalBasisFunctions(double*, double, int) const;

    /***********************************************************************************************
     * \brief Compute the non-zero basis functions and their derivatives of the  at the given parameter and stores them in a double array
     * \param[in/out] _basisFctsAndDerivs The non-zero basis functions at the given knot span up to their _derivDegree-th derivatives
     * \param[in] _derivDegree For Which degree the B-Spline derivative to be computed
     * \param[in] _uPrm The parameter where the basis functions and their derivatives are evaluated
     * \param[in] _KnotSpanIndex The index of the knot span where _uPrm lives in
     * \author Andreas Apostolatos
     ***********/
    void computeLocalBasisFunctionsAndDerivatives(double*, int, double, int) const;

    /// Get and set functions
public:
    /***********************************************************************************************
     * \brief Returns the polynomial degree of the B-Spline 1D basis
     * \author Andreas Apostolatos
     ***********/
    inline int getPolynomialDegree() const {
        return PDegree;
    }

    /***********************************************************************************************
     * \brief Returns the number of knots for the 1D B-Spline basis
     * \author Andreas Apostolatos
     ***********/
    inline int getNoKnots() const {
        return NoKnots;
    }

    /***********************************************************************************************
     * \brief Returns the knot vector of the B-Spline 1D basis
     * \author Andreas Apostolatos
     ***********/
    double* getKnotVector() const {
        return KnotVector;
    }

    /***********************************************************************************************
     * \brief Get the first knot of the vector
     * \author Fabien Pean
     ***********/
    inline double getFirstKnot() const {
        return KnotVector[0];
    }
    /***********************************************************************************************
     * \brief Get last knot of the vector
     * \author Fabien Pean
     ***********/
    inline double getLastKnot() const{
        return KnotVector[NoKnots-1];
    }
    /***********************************************************************************************
     * \brief Sets the polynomial degree of the B-Spline 1D basis
     * \author Andreas Apostolatos
     ***********/
    void setPolynomialDegree(int _pDegree) {
        PDegree = _pDegree;
    }

    /***********************************************************************************************
     * \brief Sets the number of knots for the 1D B-Spline basis
     * \param[in] _noKnots The number of knots for the new knot vector
     * \param[in] _knotVector The new knot vector to be assigned
     * \author Andreas Apostolatos
     ***********/
    void setKnotVector(int, double*);

    /// The tolerance for accepting a very small number as interior to the knot span
    static const double EPS_ACCPETEDINTOKNOTSPAN;
};

/***********************************************************************************************
 * \brief Allows for nice debug output
 * \author Chenshen Wu
 ***********/
Message &operator<<(Message &message, BSplineBasis1D &bSplineBasis1D);

}/* namespace EMPIRE */

#endif /* BSPLINEBASIS1D_H_ */
