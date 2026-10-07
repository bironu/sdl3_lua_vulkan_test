#pragma once
#if !defined(FPOINT_H_)
#define FPOINT_H_

#include "Point.h"
#include <SDL3/SDL_rect.h>

class FPoint final : public SDL_FPoint
{
public:
	FPoint(decltype(x) x_pos, decltype(y) y_pos)
		: SDL_FPoint{x_pos, y_pos}
	{
	}
	FPoint()
		: FPoint(0.0f, 0.0f)
	{
	}
	explicit FPoint(const Point &point)
		: FPoint(static_cast<decltype(x)>(point.get_x_pos()), static_cast<decltype(y)>(point.get_y_pos()))
	{
	}
	~FPoint() = default;
	decltype(x) get_x_pos() const { return x;}
	decltype(y) get_y_pos() const { return y;}
	void set_x_pos(decltype(x) x_pos){ x = x_pos;}
	void set_y_pos(decltype(y) y_pos){ y = y_pos;}
	void set_point(decltype(x) x_pos, decltype(y) y_pos)
	{
		set_x_pos(x_pos);
		set_y_pos(y_pos);
	}

	const FPoint &operator+=(const FPoint &pos)
	{
		set_x_pos(get_x_pos()+pos.get_x_pos());
		set_y_pos(get_y_pos()+pos.get_y_pos());
		return *this;
	}
};

inline const FPoint operator+(const FPoint &l, const FPoint &r)
{
	return {l.get_x_pos()+r.get_x_pos(), l.get_y_pos()+r.get_y_pos()};
}

#endif // FPOINT_H_
