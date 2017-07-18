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
 * \file MathLibrary.h
 * The header file of math functions in EMPIRE.
 * \date 4/19/2013
 **************************************************************************************************/
#ifndef MATHLIBRARY_H_
#define MATHLIBRARY_H_

#include "FEMMath.h"
#include "MatrixVectorMath.h"
#include "GeometryMath.h"
#include "ConstantsAndVariables.h"
#include "DebugMath.h"

namespace EMPIRE {
namespace MathLibrary {

/***********************************************************************************************
 * \brief Receive the section mesh from a real client
 * \param[in] _num The double to be rounded up to the next integer
 * \return The rounded up value to the next integer
 * \author Altug Emiroglu, Andreas Apostolatos
 ***********/
int ceil(double _num);

} /* namespace Math */
} /* namespace EMPIRE */
#endif /* MATHLIBRARY_H_ */
