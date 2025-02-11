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
 * \file AuxiliaryFunctions.h
 * This file holds the class of AuxiliaryFunctions.
 * \date 1/4/2013
 **************************************************************************************************/
#ifndef AUXILIARYFUNCTIONS_H_
#define AUXILIARYFUNCTIONS_H_
#include <string>

namespace EMPIRE {
/********//**
 * \brief Class AuxiliaryFunctions provides a central place for service routines
 ***********/
class AuxiliaryFunctions {
public:
    /***********************************************************************************************
     * \brief A helper function to compare string case insensitive version
     * \param[in] strFirst first standard string to be compared
     * \param[in] strSecond second standard string to be compared
     * \return true is the to strings match
     * \author Stefan Sicklinger
     ***********/
    static bool CompareStringInsensitive(std::string strFirst, std::string strSecond);
    /***********************************************************************************************
     * \brief Prints number of threads currently used
     * \param[in] level the nested parallel level
     * \author Stefan Sicklinger
     ***********/
    static void report_num_threads(int level);

};

} /* namespace EMPIRE */
#endif /* AUXILIARYFUNCTIONS_H_ */
