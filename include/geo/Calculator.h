#if !defined(GEO_CALCULATOR_H_)
#define GEO_CALCULATOR_H_

#include "Matrix.h"
#include "Vector3.h"
#include "Quaternion.h"
#include <cmath>

namespace geo
{
    // template<typename T>
	// inline Vector3<T> operator*(const Vector3<T>& vec, const Matrix4x4<T>& mat, const T w = static_cast<T>(1.0))
    // {
    //     return {
    //         vec.getX() * mat.mat[0][0] + vec.getY() * mat.mat[1][0] + vec.getZ() * mat.mat[2][0] + w * mat.mat[3][0],
    //         vec.getX() * mat.mat[0][1] + vec.getY() * mat.mat[1][1] + vec.getZ() * mat.mat[2][1] + w * mat.mat[3][1],
    //         vec.getX() * mat.mat[0][2] + vec.getY() * mat.mat[1][2] + vec.getZ() * mat.mat[2][2] + w * mat.mat[3][2]
    //     };
    // }

	template<typename T>
	inline Matrix4x4<T> createIdentityMatrix4x4()
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const std::array<T, 16> data = {
			one,  zero, zero, zero,
			zero, one,  zero, zero,
			zero, zero, one,  zero,
			zero, zero, zero, one,
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createScale(const T &xScale, const T &yScale, const T &zScale)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const std::array<T, 16> data = {
			xScale, zero,   zero,   zero,
			zero,   yScale, zero,   zero,
			zero,   zero,   zScale, zero,
			zero,   zero,   zero,   one
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createScale(const Vector3<T> &scaleVector)
	{
		return createScale(scaleVector.getX(), scaleVector.getY(), scaleVector.getZ());
	}

    template<typename T>
	inline Matrix4x4<T> createScale(const T &scale)
	{
		return createScale(scale, scale, scale);
	}

	template<typename T>
	inline Matrix4x4<T> createRotationX(const T &theta)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const auto c = std::cos(theta);
		const auto s = std::sin(theta);
		const std::array<T, 16> data = {
			one,  zero, zero, zero,
			zero, c,    s,    zero,
			zero, -s,   c,    zero,
			zero, zero, zero, one,
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createRotationY(const T &theta)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const auto c = std::cos(theta);
		const auto s = std::sin(theta);
		const std::array<T, 16> data = {
			c,    zero, -s,   zero,
			zero, one,  zero, zero,
			s,    zero, c,    zero,
			zero, zero, zero, one,
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createRotationZ(const T &theta)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const auto c = std::cos(theta);
		const auto s = std::sin(theta);
		const std::array<T, 16> data = {
			c,    s,    zero, zero,
			-s,   c,    zero, zero,
			zero, zero, one,  zero,
			zero, zero, zero, one,
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createFromQuaternion(const geo::Quaternion<T> &q)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const T two = static_cast<T>(2.0);
		const T xx2 = two * q.getX() * q.getX();
		const T yy2 = two * q.getY() * q.getY();
		const T zz2 = two * q.getZ() * q.getZ();
		const T xy2 = two * q.getX() * q.getY();
		const T wz2 = two * q.getW() * q.getZ();
		const T xz2 = two * q.getX() * q.getZ();
		const T wy2 = two * q.getW() * q.getY();
		const T yz2 = two * q.getY() * q.getZ();
		const T wx2 = two * q.getW() * q.getX();
		const std::array<T, 16> data = {
			one - yy2 - zz2, xy2 + wz2,       xz2 - wy2,       zero,
			xy2 - wz2,       one - xx2 - zz2, yz2 + wx2,       zero,
			xz2 + wy2,       yz2 - wx2,       one - xx2 - yy2, zero,
			zero,            zero,            zero,            one
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createTranslation(const Vector3<T> &trans)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const std::array<T, 16> data = {
			one,          zero,         zero,         zero,
			zero,         one,          zero,         zero,
			zero,         zero,         one,          zero,
			trans.getX(), trans.getY(), trans.getZ(), one,
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createLookAt(const Vector3<T> &eye, const Vector3<T> &target, const Vector3<T> &up)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const Vector3<T> zaxis = Vector3<T>::normalize(eye - target);
		const Vector3<T> xaxis = Vector3<T>::normalize(Vector3<T>::cross(up, zaxis));
		const Vector3<T> yaxis = Vector3<T>::normalize(Vector3<T>::cross(zaxis, xaxis));
		const Vector3<T> trans{
			-Vector3<T>::dot(xaxis, eye),
			-Vector3<T>::dot(yaxis, eye),
			-Vector3<T>::dot(zaxis, eye)
		};
		const std::array<T, 16> data = {
			xaxis.getX(), yaxis.getX(), zaxis.getX(), zero,
			xaxis.getY(), yaxis.getY(), zaxis.getY(), zero,
			xaxis.getZ(), yaxis.getZ(), zaxis.getZ(), zero,
			trans.getX(), trans.getY(), trans.getZ(), one,
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createOrtho(const T &width, const T &height, const T &near, const T &far)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const T two = static_cast<T>(2.0);
		const std::array<T, 16> data = {
			two / width, zero,         zero,                         zero,
			zero,        two / height, zero,                         zero,
			zero,        zero,         -two / (far - near),          zero,
			zero,        zero,         -(far + near) / (far - near), one,
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createPerspective(const T &fovY, const T &width, const T &height, const T &near, const T &far)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const T two = static_cast<T>(2.0);
		const T divtwo = one / two;
		const T yScale = one / std::tan(fovY * divtwo);
		const T xScale = yScale * height / width;
		const std::array<T, 16> data = {
			xScale, zero,   zero,                               zero,
			zero,   yScale, zero,                               zero,
			zero,   zero,   -(far + near) / (far - near),       -one,
			zero,   zero,   -(two * far * near) / (far - near), zero,
		};
		return Matrix4x4<T>(data);
	}

	template<typename T>
	inline Matrix4x4<T> createFrustum(const T &left, const T &right, const T &bottom, const T &top, const T &near, const T &far)
	{
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const T two = static_cast<T>(2.0);
		const T width = right - left;
		const T height = top - bottom;
		const std::array<T, 16> data = {
			two * near / width, zero,         zero, zero,
			zero,        two / height, zero, zero,
			(left + right) / width,        (top + bottom) / height,         -(far + near) / (far - near),  -one,
			zero,        zero,         -(two * far * near) / (far - near),  one,
		};
		return Matrix4x4<T>(data);
	}

    template <typename T>
    inline T lerp(const T &a, const T &b, const T &f)
	{
		return a + f * (b - a);
	}

}

#endif // GEO_CALCULATOR_H_
