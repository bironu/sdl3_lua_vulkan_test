#pragma once
#if !defined(FRECT_H_)
#define FRECT_H_

#include "FPoint.h"
#include "Vector2.h"
#include <SDL3/SDL_rect.h>

class FRect final : public SDL_FRect
{
public:
	FRect()
		: SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f}
	{
	}
	FRect(decltype(x) x_pos, decltype(y) y_pos, decltype(w) width, decltype(h) height)
		: SDL_FRect{x_pos, y_pos, width, height}
	{
	}
	FRect(const FPoint &point, const geo::Sizef &size)
		: SDL_FRect{point.get_x_pos(), point.get_y_pos(), size.getWidth(), size.getHeight()}
	{
	}
	explicit FRect(const Rect &rect)
		: FRect(static_cast<decltype(x)>(rect.getXPos()), static_cast<decltype(y)>(rect.getYPos()), static_cast<decltype(w)>(rect.getWidth()), static_cast<decltype(h)>(rect.getHeight()))
	{
	}
	~FRect() = default;

	decltype(x) getXPos() const { return x;}
	decltype(y) getYPos() const { return y;}
	decltype(w) getWidth() const { return w;}
	decltype(h) getHeight() const { return h;}

	const geo::Sizef getSize() const { return {getWidth(), getHeight()}; }
	const FPoint getPos() const { return {getXPos(), getYPos()}; }

	bool isEmpty() const { return ::SDL_RectEmptyFloat(this);}
	bool is_point_in_rect(decltype(x) x_pos, decltype(y) y_pos) const
	{
		return ((getXPos() <= x_pos) && (x_pos < (getXPos() + getWidth())) && (getYPos() <= y_pos) && (y_pos < (getYPos() + getHeight())));
	}

	bool is_point_in_rect(const FPoint &point) const
	{
		return is_point_in_rect(point.get_x_pos(), point.get_y_pos());
	}

	void setXPos(decltype(x) x_pos){ x = x_pos;}
	void setYPos(decltype(y) y_pos){ y = y_pos;}
	void setWidth(decltype(w) width){ w = width;}
	void setHeight(decltype(h) height){ h = height;}
	void setRect(decltype(x) x_pos, decltype(y) y_pos, decltype(w) width, decltype(h) height)
	{
		setXPos(x_pos);
		setYPos(y_pos);
		setWidth(width);
		setHeight(height);
	}
	void setRectEmpty(){ setRect(0.0f, 0.0f, 0.0f, 0.0f);}

	void offsetRect(decltype(x) add_x, decltype(y) add_y)
	{
		setXPos(getXPos() + add_x);
		setYPos(getYPos() + add_y);
	}

	void operator=(const FRect &rect){ setRect(rect.x, rect.y, rect.w, rect.h);}
	bool operator==(const FRect &rect) const
	{
		return ::SDL_RectsEqualFloat(this, &rect);
	}
	bool operator!=(const FRect &rect) const { return !(*this == rect);}

	const FRect operator&(const FRect &rect) const
	{
		FRect result;
		if(!::SDL_GetRectIntersectionFloat(this, &rect, &result)){
			result.setRectEmpty();
		}
		return result;
	}
	const FRect operator|(const FRect &rect) const
	{
		FRect result;
		::SDL_GetRectUnionFloat(this, &rect, &result);
		return result;
	}
	bool operator&&(const FRect &rect) const
	{
		return ::SDL_HasRectIntersectionFloat(this, &rect);
	}
};

#endif // FRECT_H_
