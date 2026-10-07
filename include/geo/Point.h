#pragma once
#if !defined(POINT_H_)
#define POINT_H_

#include <SDL3/SDL_rect.h>

class RectTest;

class Point final : public SDL_Point
{
public:
	Point(decltype(x) x_pos, decltype(y) y_pos)
		: SDL_Point{x_pos, y_pos}
	{
	}
	Point()
		: Point(0, 0)
	{
	}
	~Point() = default;
	decltype(x) get_x_pos() const { return x;}
	decltype(y) get_y_pos() const { return y;}
	void set_x_pos(decltype(x) x_pos){ x = x_pos;}
	void set_y_pos(decltype(y) y_pos){ y = y_pos;}
	void set_point(decltype(x) x_pos, decltype(y) y_pos)
	{
		set_x_pos(x_pos);
		set_y_pos(y_pos);
	}

	const Point &operator+=(const Point &pos)
	{
		set_x_pos(get_x_pos()+pos.get_x_pos());
		set_y_pos(get_y_pos()+pos.get_y_pos());
		return *this;
	}
};

inline const Point operator+(const Point &l, const Point &r)
{
	return {l.get_x_pos()+r.get_x_pos(), l.get_y_pos()+r.get_y_pos()};
}

#endif // POINT_H_
