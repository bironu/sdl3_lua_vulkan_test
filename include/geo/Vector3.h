#pragma once
#if !defined(GEO_VECTOR3_H_)
#define GEO_VECTOR3_H_

#include "Vector2.h"
#include <cmath>

namespace geo
{
template<typename T>
class Vector3
{
public:
	using type = T;

	Vector3()
		: val_{}
	{}

	Vector3(const T &x, const T &y, const T &z)
		: val_{x, y, z}
	{}

	Vector3(const Vector2<T> &vec2, const T &z)
		:val_{vec2.getX(), vec2.getY() ,z}
	{}

    void setX(const T &x) { val_[0] = x; }
    void setY(const T &y) { val_[1] = y; }
    void setZ(const T &z) { val_[2] = z; }
	void set(const T &x, const T &y, const T &z)
	{
    	setX(x);
    	setY(y);
    	setZ(z);
	}

    T getX() const { return val_[0]; }
    T getY() const { return val_[1]; }
    T getZ() const { return val_[2]; }

	// Cast to a const T pointer
	const T *ptr() const
	{
		return val_;
	}

	Vector3 operator-() const
	{
		return Vector3(-getX(), -getY(), -getZ());
	}

	// Scalar *=
	Vector3 &operator*=(const T &scalar)
	{
		setX(getX() * scalar);
		setY(getY() * scalar);
		setZ(getZ() * scalar);
		return *this;
	}

	// Scalar /=
	Vector3 &operator/=(const T &scalar)
	{
		setX(getX() / scalar);
		setY(getY() / scalar);
		setZ(getZ() / scalar);
		return *this;
	}

	// Vector +=
	Vector3 &operator+=(const Vector3 &right)
	{
		setX(getX() + right.getX());
		setY(getY() + right.getY());
		setZ(getZ() + right.getZ());
		return *this;
	}

	// Vector -=
	Vector3 &operator-=(const Vector3 &right)
	{
		setX(getX() - right.getX());
		setY(getY() - right.getY());
		setZ(getZ() - right.getZ());
		return *this;
	}

	// Length squared of vector
	T lengthSquared() const
	{
		return (getX() * getX() + getY() * getY() + getZ() * getZ());
	}

	// Length of vector
	T getLength() const
	{
		return std::pow(lengthSquared(), static_cast<T>(0.5));
	}

	// Normalize this vector
	void normalizeSelf()
	{
		const T invLength = static_cast<T>(1.0) / getLength();
		setX(getX() * invLength);
		setY(getY() * invLength);
		setZ(getZ() * invLength);
    }

	// Normalize the provided vector
	static Vector3 normalize(const Vector3 &vec)
	{
		Vector3 temp = vec;
		temp.normalizeSelf();
		return temp;
	}

	// Dot product between two vectors (a dot b)
	static T dot(const Vector3 &lh, const Vector3 &rh)
	{
		return (lh.getX() * rh.getX() + lh.getY() * rh.getY() + lh.getZ() * rh.getZ());
	}

	// Cross product between two vectors (a cross b)
	static Vector3 cross(const Vector3 &lh, const Vector3 &rh)
	{
		return {
            (lh.getY() * rh.getZ() - lh.getZ() * rh.getY()),
            (lh.getZ() * rh.getX() - lh.getX() * rh.getZ()),
            (lh.getX() * rh.getY() - lh.getY() * rh.getX())
        };
	}

		// static Vector3 Transform(const Vector3& vec, const class Matrix4& mat, const T w = static_cast<T>(1.0))
    // {
    //     return {
    //         vec.getX() * mat.mat[0][0] + vec.getY() * mat.mat[1][0] + vec.getZ() * mat.mat[2][0] + w * mat.mat[3][0],
    //         vec.getX() * mat.mat[0][1] + vec.getY() * mat.mat[1][1] + vec.getZ() * mat.mat[2][1] + w * mat.mat[3][1],
    //         vec.getX() * mat.mat[0][2] + vec.getY() * mat.mat[1][2] + vec.getZ() * mat.mat[2][2] + w * mat.mat[3][2]
    //     };
    // }

	// // This will transform the vector and renormalize the w component
	// static Vector3 TransformWithPerspDiv(const Vector3& vec, const class Matrix4& mat, const T w = static_cast<T>(1.0))
    // {
    //     Vector3 retVal = Transform(vec, mat, w);
    //     const T transformedW = vec.getX() * mat.mat[0][3] + vec.getY() * mat.mat[1][3] + vec.getZ() * mat.mat[2][3] + w * mat.mat[3][3];
    //     if (!Math::NearZero(Math::Abs(transformedW)))
    //     {
    //         transformedW = static_cast<T>(1.0) / transformedW;
    //         retVal *= transformedW;
    //     }
    //     return retVal;
    // }

	// // Transform a Vector3 by a quaternion
	// static Vector3 Transform(const Vector3& v, const class Quaternion& q)
    // {
    //     // v + 2.0*cross(q.xyz, cross(q.xyz,v) + q.w*v);
    //     Vector3 qv(q.x, q.y, q.z);
    //     Vector3 retVal = v;
    //     retVal += static_cast<T>(2.0) * Vector3::Cross(qv, Vector3::Cross(qv, v) + q.w * v);
    //     return retVal;
    // }

	// static const Vector3 Zero;
	// static const Vector3 UnitX;
	// static const Vector3 UnitY;
	// static const Vector3 UnitZ;
	// static const Vector3 NegUnitX;
	// static const Vector3 NegUnitY;
	// static const Vector3 NegUnitZ;
	// static const Vector3 Infinity;
	// static const Vector3 NegInfinity;

private:
	T val_[3];
};

// Vector addition (lh + rh)
template <typename T>
inline Vector3<T> operator+(const Vector3<T> &lh, const Vector3<T> &rh)
{
	return {
		lh.getX() + rh.getX(),
		lh.getY() + rh.getY(),
		lh.getZ() + rh.getZ()
	};
}

// Vector subtraction (lh - rh)
template <typename T>
inline Vector3<T> operator-(const Vector3<T> &lh, const Vector3<T> &rh)
{
	return {
		lh.getX() - rh.getX(),
		lh.getY() - rh.getY(),
		lh.getZ() - rh.getZ()
	};
}

// Component-wise multiplication
template <typename T>
inline Vector3<T> operator*(const Vector3<T> &lh, const Vector3<T> &rh)
{
	return {
		lh.getX() * rh.getX(),
		lh.getY() * rh.getY(),
		lh.getZ() * rh.getZ()
	};
}

// Scalar multiplication
template <typename T>
inline Vector3<T> operator*(const Vector3<T> &vec, const T &scalar)
{
	return {
		vec.getX() * scalar,
		vec.getY() * scalar,
		vec.getZ() * scalar
	};
}

// Scalar multiplication
template <typename T>
inline Vector3<T> operator*(const T &scalar, const Vector3<T> &vec)
{
	return vec * scalar;
}

//2つのベクトルのなす角度
template <typename T>
inline T radian(const Vector3<T> &u,const Vector3<T> &v)
{
	const T cos = (u * v) / (u.getLength() * v.getLength());
	return std::acos(cos);
}

// Lerp from A to B by f
template <typename T>
inline Vector3<T> lerp(const Vector3<T> &a, const Vector3<T> &b, const T &f)
{
	return Vector3(a + f * (b - a));
}

// Reflect V about (normalized) N
template <typename T>
inline Vector3<T> Reflect(const Vector3<T> &v, const Vector3<T> &n)
{
	return v - static_cast<T>(2.0) * Vector3<T>::dot(v, n) * n;
}


using Vector3f = Vector3<float>;
using Vector3d = Vector3<double>;

} // namespace geo

#endif // GEO_VECTOR3_H_
