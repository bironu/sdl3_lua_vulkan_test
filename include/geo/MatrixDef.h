#pragma once
#if !defined(GEO_MATRIXDEF_H_)
#define GEO_MATRIXDEF_H_

namespace geo
{
template<int Row, int Col>
class ColumnMejor;

template<int Row, int Col>
class RowMejor;

template <typename T, int Row, int Col, template <int, int> class Mejor>
class Matrix;

template <typename T>
using Matrix4x4 = Matrix<T, 4, 4, ColumnMejor>;
using Matrix4x4f = Matrix4x4<float>;
using Matrix4x4d = Matrix4x4<double>;

}

#endif // GEO_MATRIXDEF_H_
