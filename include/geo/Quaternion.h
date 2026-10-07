#ifndef GRAPHICS_QUATERNION_H_
#define GRAPHICS_QUATERNION_H_

#include "Vector3.h"

namespace geo
{

template<typename T>
class Quaternion
{
public:
	Quaternion()
		: w_(static_cast<T>(1.0))
		, v_()
	{
	}

	Quaternion(const T &w, const T &x, const T &y, const T &z)
		: w_(w)
		, v_(x, y, z)
	{
	}

	Quaternion(const T &w, const Vector3<T> &v)
		: w_(w)
		, v_(v)
	{
	}

	~Quaternion()
	{
	}

	T getW() const { return w_; }
	T getX() const { return v_.getX(); }
	T getY() const { return v_.getY(); }
	T getZ() const { return v_.getZ(); }
	const Vector3<T> &getV() const { return v_; }

	void setW(const T &w) { w_ = w; }
	void setX(const T &x) { v_.setX(x); }
	void setY(const T &y) { v_.setY(y); }
	void setZ(const T &z) { v_.setZ(z); }
	void setV(const Vector3<T> &v) { v_ = v; }

	void setIdentity()
	{
		setW(static_cast<T>(1.0));
		setV(Vector3<T>());
	}

	static Quaternion createIdentity() { return Quaternion<T>(); }

	// 共役Quaternion
	//void conjugate() { setV(-getV()); }
	static Quaternion createConjugate(const Quaternion &q) { return {q.getW(), -q.getV()}; }

	T lengthSquared() const { return getW() * getW() + getV().lengthSquared(); }
	T getLength() const { return std::pow(lengthSquared(), static_cast<T>(0.5)); }

	void normalizeSelf()
	{
		const T invLength = static_cast<T>(1.0) / getLength();
		setW(getW() * invLength);
		setV(getV() * invLength);
	}

	static Quaternion normalize(const Quaternion &q)
	{
		auto r = q;
		r.normalizeSelf();
		return r;
	}

	static T dot(const Quaternion &a, const Quaternion &b)
	{
		return a.getX() * b.getX() + a.getY() * b.getY() + a.getZ() * b.getZ() + a.getW() * b.getW();
	}

	static Quaternion createRotater(const T &radian, const Vector3<T> &axis)
	{
		//原点を中心とした軸ベクトル = (x, y, z)
		//回転する角度 = th
		//Q = (cos(th / 2); x * sin(th / 2), y * sin(th / 2), z * sin(th / 2))
		const T th2 = radian * static_cast<T>(0.5);
		return {std::cos(th2), axis * std::sin(th2)};
	}

	// Linear interpolation
	static Quaternion lerp(const Quaternion &a, const Quaternion &b, const T &f)
	{
		Quaternion retVal{
			lerp(a.getW(), b.getW(), f),
			lerp(a.getX(), b.getX(), f),
			lerp(a.getY(), b.getY(), f),
			lerp(a.getZ(), b.getZ(), f)
		};
		retVal.normalizeSelf();
		return retVal;
	}

	// Spherical Linear Interpolation
	static Quaternion slerp(const Quaternion& a, const Quaternion& b, const T &f)
	{
		const T threshold = static_cast<T>(0.99999);
		const T zero = static_cast<T>(0.0);
		const T one = static_cast<T>(1.0);
		const T rawCosm = Quaternion::dot(a, b);
		const T cosom = std::abs(rawCosm);

		T scale0, scale1;
		if (cosom < threshold) {
			const T omega = std::acos(cosom);
			const T invSin = one / std::sin(omega);
			scale0 = std::sin((one - f) * omega) * invSin;
			scale1 = std::sin(f * omega) * invSin;
		}
		else {
			// Use linear interpolation if the quaternions
			// are collinear
			scale0 = one - f;
			scale1 = f;
		}

		if (rawCosm < zero)
		{
			scale1 = -scale1;
		}

		Quaternion retVal {
			scale0 * a.getW() + scale1 * b.getW(),
			scale0 * a.getX() + scale1 * b.getX(),
			scale0 * a.getY() + scale1 * b.getY(),
			scale0 * a.getZ() + scale1 * b.getZ(),
		};
		retVal.normalizeSelf();
		return retVal;
	}

private:
	T w_;
	Vector3<T> v_;
};

template <typename T>
inline Quaternion<T> operator*(const Quaternion<T> &lh, const Quaternion<T> &rh)
{
	// Q = (q; V)
	// R = (r; W)
	// QR = (qr - V・W; qW + rV + V × W)
	return {lh.getW() * rh.getW() - Vector3<T>::dot(lh.getV(), rh.getV()),
		lh.getW() * rh.getV() + rh.getW() * lh.getV() + Vector3<T>::cross(lh.getV(), rh.getV())
	};
}

using Quaternionf = Quaternion<float>;
using Quaterniond = Quaternion<double>;

} // namespace geo

#endif // GRAPHICS_QUATERNION_H_
