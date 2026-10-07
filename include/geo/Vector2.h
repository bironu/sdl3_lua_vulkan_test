#if !defined(GEO_VECTOR2_H_)
#define GEO_VECTOR2_H_

#include <cmath>

namespace geo
{
template <typename T>
class Vector2
{
public:
	using type = T;

	Vector2()
		: x_()
		, y_()
	{}

	Vector2(const T &x, const T &y)
		: x_(x)
		, y_(y)
	{}

	void setX(const T &x) { x_ = x; }
	void setY(const T &y) { y_ = y; }
	void setWidth(const T &w) { setX(w); }
	void setHeight(const T &h) { setY(h); }
	void set(const T &x, const T &y)
	{
		setX(x);
		setY(y);
	}

	const T &getX() const { return x_; }
	const T &getY() const { return y_; }
	const T &getWidth() const { return getX(); }
	const T &getHeight() const { return getY(); }

	// Scalar *=
	Vector2 &operator*=(const T &scalar)
	{
		setX(getX() * scalar);
		setY(getY() * scalar);
		return *this;
	}

	// Vector +=
	Vector2 &operator+=(const Vector2 &right)
	{
		setX(getX() + right.getX());
		setY(getY() + right.getY());
		return *this;
	}

	// Vector -=
	Vector2 &operator-=(const Vector2 &right)
	{
		setX(getX() - right.getX());
		setY(getY() - right.getY());
		return *this;
	}

	// Length squared of vector
	T lengthSquared() const
	{
		return (getX() * getX() + getY() * getY());
	}

	// Length of vector
	T getLength() const
	{
		return std::pow(lengthSquared(), static_cast<T>(0.5));
	}

	// Normalize this vector
	void normalize()
	{
		const T length = getLength();
		setX(getX() / length);
		setY(getY() / length);
	}

	// Normalize the provided vector
	static Vector2 normalize(const Vector2 &vec)
	{
		auto temp = vec;
		temp.normalize();
		return temp;
	}

	// Dot product between two vectors (a dot b)
	static T dot(const Vector2 &a, const Vector2 &b)
	{
		return (a.getX() * b.getX() + a.getY() * b.getY());
	}

	// Lerp from A to B by f
	static Vector2 lerp(const Vector2 &a, const Vector2 &b, const T &f)
	{
		return Vector2(a + f * (b - a));
	}
	
	// // Reflect V about (normalized) N
	// template <typename T>
	// static Vector2<T> reflect(const Vector2<T> &v, const Vector2<T> &n)
	// {
	// 	return v - static_cast<T>(2.0) * Vector2<T>::dot(v, n) * n;
	// }

	// // Transform vector by matrix
	// template <typename T>
	// static Vector2<T> transform(const Vector2<T> &vec, const class Matrix3<T>& mat, float w = 1.0f);

	// static const Vector2 Zero;
	// static const Vector2 UnitX;
	// static const Vector2 UnitY;
	// static const Vector2 NegUnitX;
	// static const Vector2 NegUnitY;

private:
	T x_;
	T y_;
};

// Vector addition (a + b)
template <typename T>
inline Vector2<T> operator+(const Vector2<T> &a, const Vector2<T> &b)
{
	return Vector2<T>(a.getX() + b.getX(), a.getY() + b.getY());
}

// Vector subtraction (a - b)
template <typename T>
inline Vector2<T> operator-(const Vector2<T> &a, const Vector2<T> &b)
{
	return Vector2<T>(a.getX() - b.getX(), a.getY() - b.getY());
}

// Component-wise multiplication
// (a.x * b.x, ...)
template <typename T>
inline Vector2<T> operator*(const Vector2<T> &a, const Vector2<T> &b)
{
	return Vector2<T>(a.getX() * b.getX(), a.getY() * b.getY());
}

// Scalar multiplication
template <typename T>
inline Vector2<T> operator*(const Vector2<T> &vec, const T &scalar)
{
	return Vector2<T>(vec.getX() * scalar, vec.getY() * scalar);
}

// Scalar multiplication
template <typename T>
inline Vector2<T> operator*(const T &scalar, const Vector2<T> &vec)
{
	return Vector2<T>(vec.getX() * scalar, vec.getY() * scalar);
}

using Vector2f = Vector2<float>;
using Vector2d = Vector2<double>;
using Vector2i = Vector2<int>;
using Sized = Vector2d;
using Sizef = Vector2f;
using Sizei = Vector2i;

}

#endif // GEO_VECTOR2_H_
