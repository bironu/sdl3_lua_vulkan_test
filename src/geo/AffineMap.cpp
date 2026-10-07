#include "geo/AffineMap.h"

namespace geo
{

void AffineMap::setPos(const geo::Vector3f &pos)
{
    pos_ = pos;
    isChange_ = true;
}

void AffineMap::setXPos(const float x)
{
    pos_.setX(x);
    isChange_ = true;
}

void AffineMap::setYPos(const float y)
{
    pos_.setY(y);
    isChange_ = true;
}

void AffineMap::setZPos(const float z)
{
    pos_.setZ(z);
    isChange_ = true;
}

void AffineMap::setOffset(const geo::Vector3f &offset)
{
    offset_ = offset;
    isChange_ = true;
}

void AffineMap::setXOffset(const float x)
{
    offset_.setX(x);
    isChange_ = true;
}

void AffineMap::setYOffset(const float y)
{
    offset_.setY(y);
    isChange_ = true;
}

void AffineMap::setZOffset(const float z)
{
    offset_.setZ(z);
    isChange_ = true;
}

void AffineMap::setScale(const geo::Vector3f &scale)
{
    scale_ = scale;
    isChange_ = true;
}

void AffineMap::setWidth(const float width)
{
    scale_.setX(width);
    isChange_ = true;
}

void AffineMap::setHeight(const float height)
{
    scale_.setY(height);
    isChange_ = true;
}

void AffineMap::setDepth(const float depth)
{
    scale_.setZ(depth);
    isChange_ = true;
}

void AffineMap::setRotation(const geo::Quaternionf &rotation)
{
    rotation_ = rotation;
    isChange_ = true;
}

void AffineMap::addRotation(const geo::Quaternionf &rotation)
{
    rotation_ = rotation * rotation_;
    rotation_.normalizeSelf();
    isChange_ = true;
}

void AffineMap::initRotation()
{
    rotation_ = Quaternionf::createIdentity();
    isChange_ = true;
}

const Matrix4x4f &AffineMap::getMatrix()
{
    if (isChange_) {
        matrix_ = geo::createTranslation<float>(getPos() + getOffset());
        matrix_ = matrix_ * geo::createFromQuaternion<float>(getRotation());
        matrix_ = matrix_ * geo::createScale<float>(getScale());
        isChange_ = false;
    }

    return matrix_;
}

}