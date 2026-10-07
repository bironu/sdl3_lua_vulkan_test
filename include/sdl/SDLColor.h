#ifndef COLOR_H_
#define COLOR_H_

#include <SDL3/SDL_pixels.h>

namespace SDL_
{

class Color final : public SDL_Color
{
public:
	Color() = default;
	Color(Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
		: SDL_Color{red, green, blue, alpha}
	{
	}
	~Color() = default;

	void setRed(Uint8 red) { r = red; }
	Uint8 getRed() const { return r; }
	void setGreen(Uint8 green) { g = green; }
	Uint8 getGreen() const { return g; }
	void setBlue(Uint8 blue) { b = blue; }
	Uint8 getBlue() const { return b; }
	void setAlpha(Uint8 alpha) { a = alpha; }
	Uint8 getAlpha() const { return a; }

	static const Color WHITE;
	static const Color BLACK;
	static const Color RED;
	static const Color BLUE;
	static const Color GREEN;
};

}

#endif // COLOR_H_
