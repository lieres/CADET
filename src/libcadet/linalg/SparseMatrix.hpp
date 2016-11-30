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
 * Defines a sparse matrix
 */

#ifndef LIBCADET_SPARSEMATRIX_HPP_
#define LIBCADET_SPARSEMATRIX_HPP_

#include <vector>
#include <ostream>

#include "cadet/cadetCompilerInfo.hpp"
#include "common/CompilerSpecific.hpp"

namespace cadet
{

namespace linalg
{

/**
 * @brief Represents a sparse matrix in coordinate list format (i.e., storage is a list of [row, column, value] tuples)
 * @details The elements of the SparseMatrix can be accessed by operator() and addElement(). If operator() is used, a
 *          lookup is performed first. If the element is found, it is returned. Otherwise, a new element at the given
 *          position is added. Contrary to operator(), addElement() will always add a new element and does not check if
 *          it already exists.
 *          
 *          This matrix format is meant as intermediate format for constructing a sparse matrix. Users are encouraged to
 *          convert their SparseMatrix to a CompressedSparseMatrix, which requires significantly less storage.
 */
class SparseMatrix
{
public:
	/**
	 * @brief Creates an empty SparseMatrix with capacity @c 0
	 * @details Users have to call resize() prior to populating the matrix.
	 */
	SparseMatrix() CADET_NOEXCEPT : _curIdx(0) { }

	/**
	 * @brief Creates an empty SparseMatrix with the given capacity
	 * @param [in] nnz Capacity, that is the maximum number of non-zero elements
	 */
	SparseMatrix(unsigned int nnz) : _curIdx(0) { resize(nnz); }

	~SparseMatrix() CADET_NOEXCEPT { }

	// Default copy and assignment semantics
	SparseMatrix(const SparseMatrix& cpy) = default;
	SparseMatrix(SparseMatrix&& cpy) CADET_NOEXCEPT = default;

	SparseMatrix& operator=(const SparseMatrix& cpy) = default;

#ifdef COMPILER_SUPPORT_NOEXCEPT_DEFAULTED_MOVE
	SparseMatrix& operator=(SparseMatrix&& cpy) CADET_NOEXCEPT = default;
#else
	SparseMatrix& operator=(SparseMatrix&& cpy) = default;
#endif
	
	/**
	 * @brief Resets all elements to @c 0
	 * @details The capacity of the SparseMatrix is not changed.
	 */
	inline void clear() CADET_NOEXCEPT { _curIdx = 0; }

	/**
	 * @brief Resets the maximum number of non-zero elements / the capacity
	 * @details The matrix is reset to an empty state. All previous content is lost.
	 * 
	 * @param [in] nnz Maximum number of non-zero elements
	 */
	inline void resize(unsigned int nnz)
	{
		_rows.clear();
		_rows.resize(nnz);

		_cols.clear();
		_cols.resize(nnz);

		_values.clear();
		_values.resize(nnz);

		_curIdx = 0;
	}

	/**
	 * @brief Returns the capacity, that is the maximum number of non-zero elements which can be stored in the matrix
	 * @details Note that the capacity is not the current number of non-zero elements.
	 * @return Maximum number of non-zero elements that can be stored in the matrix
	 */
	inline unsigned int capacity() const CADET_NOEXCEPT { return _rows.size(); }

	/**
	 * @brief Inserts a new element at the given position to the given value
	 * @details If the element does not exist and the capacity is not exhausted,
	 *          a new element is created. As the capacity is not increased by
	 *          this method, it will fail when the capacity is exhausted and
	 *          a new element would have to be created.
	 * 
	 * @param [in] row Row index
	 * @param [in] col Column index
	 * @param [in] val Value of the element at the given position
	 */
	inline void addElement(unsigned int row, unsigned int col, double val)
	{
		cadet_assert(_curIdx < _rows.size());

		_rows[_curIdx] = row;
		_cols[_curIdx] = col;
		_values[_curIdx] = val;

		++_curIdx;
	}

	inline double& operator()(unsigned int row, unsigned int col)
	{
		// Try to find the element
		for (unsigned int i = 0; i < _curIdx; ++i)
		{
			if ((_rows[i] == row) && (_cols[i] == col))
				return _values[i];
		}

		// Value not found, so add it
		cadet_assert(_curIdx < _rows.size());
		
		_rows[_curIdx] = row;
		_cols[_curIdx] = col;
		_values[_curIdx] = 0.0;

		++_curIdx;

		return _values[_curIdx-1];
	}

	inline const double operator()(unsigned int row, unsigned int col) const
	{
		// Try to find the element
		for (unsigned int i = 0; i < _curIdx; ++i)
		{
			if ((_rows[i] == row) && (_cols[i] == col))
				return _values[i];
		}
		return 0.0;
	}

	/**
	 * @brief Multiplies this sparse matrix with a vector and adds another vector to it
	 * @details Computes the matrix vector operation \f$y = \alpha Ax + \beta y. \f$
	 * @param [in] x Vector @f$ x @f$ to multiply with
	 * @param [in] alpha Factor @f$ \alpha @f$ in front of @f$ Ax @f$
	 * @param [in] beta Factor @f$ \beta @f$ in front of @f$ y @f$
	 * @param [in,out] out Vector @f$ y @f$ to write to
	 */
	inline void multiplyVector(double const* const x, double alpha, double beta, double* const out) const
	{
		for (unsigned int i = 0; i < _curIdx; ++i)
			out[_rows[i]] = alpha * _values[i] * x[_cols[i]] + beta * out[_rows[i]];
	}

	/**
	 * @brief Multiplies this sparse matrix with a vector and adds the result to another vector
	 * @details Computes the matrix vector operation \f$ b + Ax \f$, where the matrix vector
	 *          product is added to @p out, which is \f$ b \f$.
	 *
	 * @param [in] x Vector to multiply with
	 * @param [in,out] out Vector to add the matrix-vector product to
	 */
	inline void multiplyAdd(double const* const x, double* const out) const
	{
		for (unsigned int i = 0; i < _curIdx; ++i)
			out[_rows[i]] += _values[i] * x[_cols[i]];
	}

	/**
	 * @brief Multiplies this sparse matrix with a vector and adds the result to another vector
	 * @details Computes the matrix vector operation \f$ b - Ax \f$, where the matrix vector
	 *          product is subtracted from @p out, which is \f$ b \f$.
	 *
	 * @param [in] x Vector to multiply with
	 * @param [in,out] out Vector to subtract the matrix-vector product from
	 */
	inline void multiplySubtract(double const* const x, double* const out) const
	{
		for (unsigned int i = 0; i < _curIdx; ++i)
			out[_rows[i]] -= _values[i] * x[_cols[i]];
	}

	/**
	 * @brief Returns a vector with row indices
	 * @details Not all elements in the vector are actually set. Only the first numNonZero()
	 *          elements are used.
	 * @return Vector with row indices
	 */
	inline const std::vector<unsigned int>& rows() const CADET_NOEXCEPT { return _rows; }

	/**
	 * @brief Returns a vector with column indices
	 * @details Not all elements in the vector are actually set. Only the first numNonZero()
	 *          elements are used.
	 * @return Vector with column indices
	 */
	inline const std::vector<unsigned int>& cols() const CADET_NOEXCEPT { return _cols; }

	/**
	 * @brief Returns a vector with element values
	 * @details Not all elements in the vector are actually set. Only the first numNonZero()
	 *          elements are used.
	 * @return Vector with element values
	 */
	inline const std::vector<double>& values() const CADET_NOEXCEPT { return _values; }

	/**
	 * @brief Returns the number of (structurally) non-zero elements in the matrix
	 * @return Number of (structurally) non-zero elements in the matrix
	 */
	inline unsigned int numNonZero() const CADET_NOEXCEPT { return _curIdx; }

private:
	std::vector<unsigned int> _rows; //!< List with row indices of elements
	std::vector<unsigned int> _cols; //!< List with column indices of elements
	std::vector<double> _values; //!< List with values of elements
	unsigned int _curIdx; //!< Index of the first unused element
};

std::ostream& operator<<(std::ostream& out, const SparseMatrix& sm);


class CompressedSparseMatrix
{
public:
	CompressedSparseMatrix() CADET_NOEXCEPT : _values(nullptr) { }



	/**
	 * @brief Multiplies this sparse matrix with a vector and adds the result to another vector
	 * @details Computes the matrix vector operation \f$ b + Ax \f$, where the matrix vector
	 *          product is added to @p out, which is \f$ b \f$.
	 *
	 * @param [in] x Vector to multiply with
	 * @param [in,out] out Vector to add the matrix-vector product to
	 */
	inline void multiplyAdd(double const* const x, double* const out)
	{

	}

	/**
	 * @brief Multiplies this sparse matrix with a vector and adds the scaled result to another vector
	 * @details Computes the matrix vector operation \f$ b + \alpha Ax \f$, where the matrix vector
	 *          product is added to @p out, which is \f$ b \f$.
	 *
	 * @param [in] alpha Scaling factor
	 * @param [in] x Vector to multiply with
	 * @param [in,out] out Vector to add the matrix-vector product to
	 */
	inline void multiplyAdd(double alpha, double const* const x, double* const out)
	{

	}

	/**
	 * @brief Multiplies this sparse matrix with a vector and adds the result to another vector
	 * @details Computes the matrix vector operation \f$ b - Ax \f$, where the matrix vector
	 *          product is subtracted from @p out, which is \f$ b \f$.
	 *
	 * @param [in] x Vector to multiply with
	 * @param [in,out] out Vector to subtract the matrix-vector product from
	 */
	inline void multiplySubtract(double const* const x, double* const out)
	{

	}

protected:
	double* _values;
};

} // namespace linalg

} // namespace cadet

#endif  // LIBCADET_SPARSEMATRIX_HPP_
