// =============================================================================
//  CADET - The Chromatography Analysis and Design Toolkit
//  
//  Copyright © 2008-2016: The CADET Authors
//            Please see the AUTHORS and CONTRIBUTORS file.
//  
//  All rights reserved. This program and the accompanying materials
//  are made available under the terms of the GNU Public License v3.0 (or, at
//  your option, any later version) which accompanies this distribution, and
//  is available at http://www.gnu.org/licenses/gpl.html
// =============================================================================

/**
 * @file 
 * Provides utilities for AD vectors and matrices
 */

#ifndef LIBCADET_ADUTILS_HPP_
#define LIBCADET_ADUTILS_HPP_

#include "AutoDiff.hpp"

namespace cadet
{

namespace linalg
{
	class BandMatrix;

	namespace detail
	{
		class DenseMatrixBase;
	}
}

namespace ad
{

/**
 * @brief Sets seed vectors on an AD vector for computing a banded Jacobian
 * @details The band structure of a Jacobian is exploited by band compression.
 *          This is explained in @cite Puttmann2016.
 * @todo Provide more details
 * @param [in,out] adVec Vector of AD datatypes whose seed vectors are to be set
 * @param [in] adDirOffset Offset in the AD directions (can be used to move past parameter sensitivity directions)
 * @param [in] rows Number of Jacobian rows (length of the AD vector)
 * @param [in] lowerBandwidth Lower bandwidth (number of lower subdiagonals) of the banded Jacobian
 * @param [in] upperBandwidth Upper bandwidth (number of upper superdiagonals) of the banded Jacobian
 * @param [in] diagDir Diagonal direction index
 */
void prepareAdVectorSeedsForBandMatrix(active* const adVec, unsigned int adDirOffset, unsigned int rows, 
	unsigned int lowerBandwidth, unsigned int upperBandwidth, unsigned int diagDir);

/**
 * @brief Extracts a band matrix from band compressed AD seed vectors
 * @details Uses the results of an AD computation with seed vectors set by prepareAdVectorSeedsForBandMatrix() to
            assemble the Jacobian which is a band matrix.
 * @param [in] adVec Vector of AD datatypes with band compressed seed vectors
 * @param [in] adDirOffset Offset in the AD directions (can be used to move past parameter sensitivity directions)
 * @param [in] diagDir Diagonal direction index
 * @param [out] mat BandMatrix to be populated with the Jacobian
 */
void extractBandedJacobianFromAd(active const* const adVec, unsigned int adDirOffset, unsigned int diagDir, linalg::BandMatrix& mat);

/**
 * @brief Extracts a dense submatrix from band compressed AD seed vectors
 * @details Uses the results of an AD computation with seed vectors set by prepareAdVectorSeedsForBandMatrix() to
            assemble a subset of the banded Jacobian into a dense matrix.
            The subset is taken from the top left element of the band matrix (i.e., the first element on the
            main diagonal).
 * @param [in] adVec Vector of AD datatypes with band compressed seed vectors pointing to the first row of the band matrix
 * @param [in] row Index of the first row to be extracted
 * @param [in] adDirOffset Offset in the AD directions (can be used to move past parameter sensitivity directions)
 * @param [in] diagDir Diagonal direction index
 * @param [in] lowerBandwidth Lower bandwidth (number of lower subdiagonals) of the banded Jacobian
 * @param [in] upperBandwidth Upper bandwidth (number of upper superdiagonals) of the banded Jacobian
 * @param [out] mat Dense matrix to be populated with the Jacobian submatrix
 */
void extractDenseJacobianFromBandedAd(active const* const adVec, unsigned int row, unsigned int adDirOffset, unsigned int diagDir, 
	unsigned int lowerBandwidth, unsigned int upperBandwidth, linalg::detail::DenseMatrixBase& mat);

/**
 * @brief Compares a banded Jacobian with an AD version derived by band compressed AD seed vectors
 * @details Uses the results of an AD computation with seed vectors set by prepareAdVectorSeedsForBandMatrix() to
            compare the results with a given banded Jacobian. The AD Jacobian is treated as base and the analytic
            Jacobian is compared against it. The relative difference
            @f[ \Delta_{ij} = \begin{cases} \left\lvert \frac{ J_{\text{ana},ij} - J_{\text{ad},ij} }{ J_{\text{ad},ij} }\right\rvert, & J_{\text{ad},ij} \neq 0 \\ 
                               \left\lvert J_{\text{ana},ij} - J_{\text{ad},ij} \right\rvert, & J_{\text{ad},ij} = 0 \end{cases} @f]
            is computed for each matrix entry. The maximum of all @f$ \Delta_{ij} @f$ is returned.
 * @param [in] adVec Vector of AD datatypes with band compressed seed vectors
 * @param [in] adDirOffset Offset in the AD directions (can be used to move past parameter sensitivity directions)
 * @param [in] diagDir Diagonal direction index
 * @param [in] mat BandMatrix populated with the analytic Jacobian
 * @return The maximum absolute relative difference between the matrix elements
 */
double compareBandedJacobianWithAd(active const* const adVec, unsigned int adDirOffset, unsigned int diagDir, const linalg::BandMatrix& mat);

/**
 * @brief Compares a dense submatrix with a band compressed AD version
 * @details Uses the results of an AD computation with seed vectors set by prepareAdVectorSeedsForBandMatrix() to
            compare the results with a given dense submatrix of the Jacobian. The AD Jacobian is treated as base
            and the analytic Jacobian is compared against it. The relative difference
            @f[ \Delta_{ij} = \begin{cases} \left\lvert \frac{ J_{\text{ana},ij} - J_{\text{ad},ij} }{ J_{\text{ad},ij} }\right\rvert, & J_{\text{ad},ij} \neq 0 \\ 
                               \left\lvert J_{\text{ana},ij} - J_{\text{ad},ij} \right\rvert, & J_{\text{ad},ij} = 0 \end{cases} @f]
            is computed for each matrix entry. The maximum of all @f$ \Delta_{ij} @f$ is returned.
            The submatrix is taken from the top left element of the band matrix (i.e., the first element on the
            main diagonal).
 * @param [in] adVec Vector of AD datatypes with band compressed seed vectors pointing to the first row of the band matrix
 * @param [in] row Index of the first row to be extracted
 * @param [in] adDirOffset Offset in the AD directions (can be used to move past parameter sensitivity directions)
 * @param [in] diagDir Diagonal direction index
 * @param [in] lowerBandwidth Lower bandwidth (number of lower subdiagonals) of the banded Jacobian
 * @param [in] upperBandwidth Upper bandwidth (number of upper superdiagonals) of the banded Jacobian
 * @param [in] mat Dense matrix populated with the dense Jacobian submatrix
 * @return The maximum absolute relative difference between the matrix elements
 */
double compareDenseJacobianWithBandedAd(active const* const adVec, unsigned int row, unsigned int adDirOffset, unsigned int diagDir, 
	unsigned int lowerBandwidth, unsigned int upperBandwidth, const linalg::detail::DenseMatrixBase& mat);

/**
 * @brief Copies the results (0th derivative) of an AD vector to a double vector
 * @param [in] adVec Source vector of AD datatypes
 * @param [out] dest Destination vector
 * @param [in] size Size of the vectors
 * @todo Check if loop unrolling is beneficial
 */
inline void copyFromAd(active const* const adVec, double* const dest, unsigned int size)
{
	for (unsigned int i = 0; i < size; ++i)
		dest[i] = static_cast<double>(adVec[i]);
}

/**
 * @brief Copies the values of a double vector into an AD vector without modifying its derivatives
 * @param [in] src Source vector
 * @param [out] adVec Destination vector of AD datatypes
 * @param [in] size Size of the vectors
 * @todo Check if loop unrolling is beneficial
 */
inline void copyToAd(double const* const src, active* const adVec, unsigned int size)
{
	for (unsigned int i = 0; i < size; ++i)
		adVec[i].setValue(src[i]);
}

/**
 * @brief Resets a vector of AD datatypes erasing both its value and its derivatives
 * @param [in,out] adVec Vector of AD datatypes to be reset
 * @param [in] size Length of the vector
 * @todo Check if loop unrolling is beneficial
 */
inline void resetAd(active* const adVec, unsigned int size)
{
	for (unsigned int i = 0; i < size; ++i)
		adVec[i] = 0.0;
}

} // namespace ad

} // namespace cadet

#endif  // LIBCADET_ADUTILS_HPP_
