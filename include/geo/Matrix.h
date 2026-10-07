#pragma once
#if !defined(GEO_MATRIX_H_)
#define GEO_MATRIX_H_

#include "MatrixDef.h"
#include <algorithm>
#include <array>
#include <iostream>

namespace geo
{

template<int Row, int Col>
class ColumnMejor
{
public:
	int operator()(int row, int col) const
	{
		return col * Row + row; // 列優先
	}
};

template<int Row, int Col>
class RowMejor
{
public:
	int operator()(int row, int col) const
	{
		return row * Col + col; // 行優先
	}
};

// Matrix
template <typename T, int Row, int Col, template <int, int> class Mejor>
class Matrix
{
public:
	Matrix()
		: val_{}
	{
		std::fill(val_.begin(), val_.end(), T());
	}

	explicit Matrix(const std::array<T, Row * Col> &data)
		: val_{data}
	{
		//std::copy(data.begin(), data.end(), val_.begin());
	}

	Matrix(const Matrix &matrix)
		: val_{matrix.val_}
	{
	}

	const Matrix &operator=(const Matrix &matrix)
	{
		std::copy(matrix.val_.begin(), matrix.val_.end(), val_.begin());
		return *this;
	}

	// Cast to a const float pointer
	const T *data() const
	{
		return val_.data();
	}

	T get(int row, int col) const { return val_[mejor_(row, col)]; }
	void set(int row, int col, const T &val) { val_[mejor_(row, col)] = val; }

	// Invert the matrix - super slow
	//void invert();

	// // Get the translation component of the matrix
	// Vector3 GetTranslation() const
	// {
	// 	return Vector3(mat[3][0], mat[3][1], mat[3][2]);
	// }
	
	// // Get the X axis of the matrix (forward)
	// Vector3 GetXAxis() const
	// {
	// 	return Vector3::Normalize(Vector3(mat[0][0], mat[0][1], mat[0][2]));
	// }

	// // Get the Y axis of the matrix (left)
	// Vector3 GetYAxis() const
	// {
	// 	return Vector3::Normalize(Vector3(mat[1][0], mat[1][1], mat[1][2]));
	// }

	// // Get the Z axis of the matrix (up)
	// Vector3 GetZAxis() const
	// {
	// 	return Vector3::Normalize(Vector3(mat[2][0], mat[2][1], mat[2][2]));
	// }

	// // Extract the scale component from the matrix
	// Vector3 GetScale() const
	// {
	// 	Vector3 retVal;
	// 	retVal.x = Vector3(mat[0][0], mat[0][1], mat[0][2]).Length();
	// 	retVal.y = Vector3(mat[1][0], mat[1][1], mat[1][2]).Length();
	// 	retVal.z = Vector3(mat[2][0], mat[2][1], mat[2][2]).Length();
	// 	return retVal;
	// }

private:
	std::array<T, Row * Col> val_;
	const Mejor<Row, Col> mejor_ = Mejor<Row, Col>();
};

template <typename T, int LHROW, int M, int RHCOL, template <int, int> class Mejor>
inline Matrix<T, LHROW, RHCOL, Mejor> operator*(const Matrix<T, LHROW, M, Mejor> &lh, const Matrix<T, M, RHCOL, Mejor> &rh)
{
	Matrix<T, LHROW, RHCOL, Mejor> result;

	for (int row = 0; row < LHROW; ++row) {
		for (int col = 0; col < RHCOL; ++col) {
			auto sum = T();
			for (int idx = 0; idx < M; ++idx) {
				sum += lh.get(row, idx) * rh.get(idx, col);
			}
			result.set(row, col, sum);
		}		
	}

	return result;
}

template <typename T, int Row, int Col, template <int, int> class Mejor>
std::ostream& operator<<(std::ostream& stream, const Matrix<T, Row, Col, Mejor>& value)
{
	for (int row = 0; row < Row; ++row) {
		for (int col = 0; col < Col; ++col) {
			stream << '[' << value.get(row, col) << "] ";
		}
		stream << std::endl;
	}
	return stream;
}

} // namespace geo

#endif // GEO_MATRIX_H_