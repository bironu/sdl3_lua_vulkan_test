#pragma once
#if !defined(AFFINE_MAP_H_)
#define AFFINE_MAP_H_

#include "misc/Uncopyable.h"
#include "Matrix.h"
#include "Vector3.h"
#include "Quaternion.h"
#include "Calculator.h"

namespace geo
{
class AffineMap
{
public:
	UNCOPYABLE(AffineMap);
	AffineMap()
		: pos_(0.0f, 0.0f, 0.0f)
		, offset_(0.0f, 0.0f, 0.0f)
		, scale_(1.0f, 1.0f, 1.0f)
		, rotation_()
		, matrix_()
		, isChange_(true)
	{		
	}

	const auto &getPos() const { return pos_; }
	const auto getXPos() const { return pos_.getX(); }
	const auto getYPos() const { return pos_.getY(); }
	const auto getZPos() const { return pos_.getZ(); }

	const auto &getOffset() const { return offset_; }
	const auto getXOffset() const { return offset_.getX(); }
	const auto getYOffset() const { return offset_.getY(); }
	const auto getZOffset() const { return offset_.getZ(); }

	const auto &getScale() const { return scale_; }
	const auto getWidth() const { return scale_.getX(); }
	const auto getHeight() const { return scale_.getY(); }
	const auto getDepth() const { return scale_.getZ(); }

	const auto &getRotation() const { return rotation_; }

	void setPos(const geo::Vector3f &pos);
	void setXPos(const float x);
	void setYPos(const float y);
	void setZPos(const float z);

	void setOffset(const geo::Vector3f &offset);
	void setXOffset(const float x);
	void setYOffset(const float y);
	void setZOffset(const float z);

	void setScale(const geo::Vector3f &scale);
	void setWidth(const float width);
	void setHeight(const float height);
	void setDepth(const float depth);

	void setRotation(const geo::Quaternionf &rotation);
	void addRotation(const geo::Quaternionf &rotation);
	void initRotation();

	const Matrix4x4f &getMatrix();
	bool isChange() const { return isChange_; }

private:
	geo::Vector3f pos_;
	geo::Vector3f offset_;
	geo::Vector3f scale_;
	geo::Quaternionf rotation_;
	geo::Matrix4x4f matrix_;
	bool isChange_;
};

}

#endif // AFFINE_MAP_H_
