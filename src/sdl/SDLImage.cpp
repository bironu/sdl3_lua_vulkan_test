#include "sdl/SDLImage.h"
#include "sdl/SDLTexture.h"

namespace SDL_
{

namespace {

// IMG_Load()はファイル本来のビット深度(kanji.bmp等の4bpp索引カラーBMPなら
// SDL_PIXELFORMAT_INDEX4MSB)のまま返す。SDL_BlitSurface()は、この4bppサーフェス
// から矩形の一部だけを切り出してblitする際、切り出し開始x座標がバイト境界に
// 揃っていない(=奇数)場合にピクセルが破損する(SDL3の制限)。1コマ23px×23pxの
// ようにセル幅が奇数のスプライトシートでは列の半分がこれに該当してしまうため、
// create_image()と同じRGBA32へ読み込み時点で変換しておき、この問題を避ける。
SDL_Surface *convertToRGBA32(SDL_Surface *loaded)
{
	if (!loaded || loaded->format == SDL_PIXELFORMAT_RGBA32) {
		return loaded;
	}
	SDL_Surface *converted = ::SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
	::SDL_DestroySurface(loaded);
	return converted;
}

} // namespace

Image::Image(int width, int height)
	: Image(::SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32))
{
}

// isColorKeyがtrueなら、画像左上(0, 0)のピクセル色をカラーキー(透過色)として設定する
Image::Image(const char * const file, bool isColorKey)
	: Image(convertToRGBA32(::IMG_Load(file)))
{
    if (isColorKey && isEnabled()) {
        lock();
        const Uint32 topLeftPixel = *static_cast<const Uint32 *>(get()->pixels);
        unlock();
        setColorKey(topLeftPixel);
    }
}

std::shared_ptr<Image> Image::fromMemory(const void *data, size_t size, bool ignoreAlpha)
{
	SDL_IOStream *io = ::SDL_IOFromConstMem(data, size);
	if(!io){
		return nullptr;
	}
	SDL_Surface *surface = convertToRGBA32(::IMG_Load_IO(io, true));
	if(!surface){
		return nullptr;
	}
	if(ignoreAlpha && ::SDL_LockSurface(surface)){
		for(int y = 0; y < surface->h; ++y){
			Uint8 *row = static_cast<Uint8 *>(surface->pixels) + static_cast<size_t>(y) * static_cast<size_t>(surface->pitch);
			for(int x = 0; x < surface->w; ++x){
				row[x * 4 + 3] = 255; // RGBA32のA
			}
		}
		::SDL_UnlockSurface(surface);
	}
	return std::make_shared<Image>(surface);
}

Image::~Image()
{
	if(isEnabled()){
		::SDL_DestroySurface(get());
	}
}

bool Image::fillRoundedBox( int xo, int yo, int w, int h, int r, Uint32 color )
{
	SDL_Surface* dst = this->get();

	const int yd = dst->pitch / ::SDL_GetPixelFormatDetails(dst->format)->bytes_per_pixel;

	const int rpsqrt2 = static_cast<int>(r / sqrt(2.0));
	//const double r2 = r * r;

	w /= 2;
	h /= 2;

	xo += w;
	yo += h;

	w -= r;
	h -= r;

	if( w < 0 || h < 0 ) {
		return false;
	}

	::SDL_LockSurface( dst );

	Uint32 *pixels = (Uint32*)( dst->pixels );

	int sy = (yo-h)*yd;
	int ey = (yo+h)*yd;
	int sx = (xo-w);
	int ex = (xo+w);
	for(int i = sy; i<=ey; i+=yd ) {
		for(int j = sx-r; j<=ex+r; j++ ) {
			pixels[i+j] = color;
		}
	}

	int d = -r;
	int x2m1 = -1;
	int y = r;
	for(int x=0; x <= rpsqrt2; x++ ) {
		x2m1 += 2;
		d += x2m1;
		if( d >= 0 ) {
			y--;
			d -= (y*2);
		}

		for(int i=sx-x; i<=ex+x; i++ ) {
			pixels[sy-y*yd + i] = color;
		}
		for(int i=sx-y; i<=ex+y; i++ ) {
			pixels[sy-x*yd + i] = color;
		}
		for(int i=sx-y; i<=ex+y; i++ ) {
			pixels[ey+x*yd + i] = color;
		}
		for(int i=sx-x; i<=ex+x; i++ ) {
			pixels[ey+y*yd + i] = color;
		}
	}
	::SDL_UnlockSurface( dst );

	return true;
}

} // SDL_
