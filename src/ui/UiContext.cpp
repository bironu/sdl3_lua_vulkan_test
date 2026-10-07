#include "ui/UiContext.h"
#include "resources/ResourcePaths.h"
#include "resources/ResourceSet.h"
#include "resources/Resources.h"
#include "sdl/SDLBitmapFont.h"
#include "sdl/SDLColor.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLTtfFont.h"
#include "sdl/SDLVulkanWindow.h"
#include <SDL3/SDL_log.h>

namespace ui
{

UiContext::UiContext(SDL_::VulkanWindow &window, Resources &res, ResourceSet &resourceSet)
	: window_(window)
	, res_(res)
	, resourceSet_(resourceSet)
	, screenWidth_(static_cast<float>(res.getScreenWidth()))
	, screenHeight_(static_cast<float>(res.getScreenHeight()))
{
	window_.setScreenSize(screenWidth_, screenHeight_);
	auto white = std::make_shared<SDL_::Image>(1, 1);
	if(white->isEnabled()){
		white->fillRect(SDL_::Color(255, 255, 255, 255));
		white_ = window_.createTexture(white);
	}
}

UiContext::~UiContext() = default;

std::shared_ptr<VulkanTexture> UiContext::image(const std::string &path)
{
	const auto found = images_.find(path);
	if(found != images_.end()){
		return found->second;
	}
	std::shared_ptr<VulkanTexture> texture;
	if(const auto image = resourceSet_.image(path)){
		texture = window_.createCachedTexture(path, image);
	}
	if(!texture){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ui: failed to load the image: %s", path.c_str());
	}
	images_[path] = texture; // 読めなかったことも覚えておく(毎フレームのログ・読み込みを避ける)
	return texture;
}

std::shared_ptr<VulkanTexture> UiContext::renderText(const std::string &text, const std::string &fontName, float fontSize)
{
	const FontInfo info = res_.getFont(fontName);
	if(info.file.empty()){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ui: no font defined: %s", fontName.c_str());
		return nullptr;
	}
	const float size = fontSize > 0.0f ? fontSize : info.size;
	const std::string key = info.file + "@" + std::to_string(size);
	auto found = fonts_.find(key);
	if(found == fonts_.end()){
		auto font = std::make_unique<SDL_::TtfFont>(ResourcePaths::resource(info.file.c_str()).c_str(), size);
		if(!font->is_font()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ui: font open error. %s (%s)", SDL_GetError(), info.file.c_str());
			font.reset(); // 開けなかったことも覚えて、毎フレーム開き直さない
		}
		found = fonts_.emplace(key, std::move(font)).first;
	}
	if(!found->second){
		return nullptr;
	}
	return window_.createTexture(found->second->renderBlendedText(text.c_str(), SDL_::Color(255, 255, 255, 255)));
}

float UiContext::BitmapGlyphs::lineHeight() const
{
	return static_cast<float>(font->getLineHeight());
}

float UiContext::BitmapGlyphs::textWidth(const std::string &text) const
{
	return static_cast<float>(font->getTextWidth(text.c_str()));
}

const UiContext::BitmapGlyphs *UiContext::bitmapFont(const std::string &fontName, float fontSize)
{
	const FontInfo info = res_.getFont(fontName);
	if(info.file.empty()){
		return nullptr;
	}
	const float size = fontSize > 0.0f ? fontSize : info.size;
	const std::string key = info.file + "@" + std::to_string(size);
	auto found = bitmapFonts_.find(key);
	if(found == bitmapFonts_.end()){
		std::unique_ptr<BitmapGlyphs> glyphs;
		auto font = std::make_unique<SDL_::BitmapFont>(ResourcePaths::resource(info.file.c_str()).c_str(), size);
		if(font->isValid()){
			auto texture = window_.createTexture(font->atlas());
			if(texture){
				glyphs = std::make_unique<BitmapGlyphs>();
				glyphs->font = std::move(font);
				glyphs->texture = std::move(texture);
			}
		}
		if(!glyphs){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ui: bitmap font error. %s (%s)", SDL_GetError(), info.file.c_str());
		}
		found = bitmapFonts_.emplace(key, std::move(glyphs)).first; // 作れなかったことも覚える
	}
	return found->second.get();
}

void UiContext::drawBitmapText(const BitmapGlyphs &glyphs, const std::string &text, float x, float y, float scale, const Color &color, float alpha)
{
	const float atlasW = static_cast<float>(glyphs.texture->width()), atlasH = static_cast<float>(glyphs.texture->height());
	const float h = glyphs.lineHeight() * scale;
	for(const char c : text){
		const auto rect = glyphs.font->glyphRect(static_cast<unsigned char>(c));
		const float w = static_cast<float>(rect.w) * scale;
		if(c != ' '){
			window_.drawSprite2DRegion(glyphs.texture, x, y, w, h, rect.x / atlasW, rect.y / atlasH, (rect.x + rect.w) / atlasW, (rect.y + rect.h) / atlasH,
				color.r, color.g, color.b, color.a * alpha);
		}
		x += w;
	}
}

std::string UiContext::translate(const std::string &key, const std::map<std::string, std::string> &vars) const
{
	return res_.translate(key, vars);
}

uint32_t UiContext::languageVersion() const
{
	return res_.languageVersion();
}

void UiContext::drawTexture3D(const std::shared_ptr<VulkanTexture> &texture, const geo::Matrix4x4f &mvp, const Color &color, float alpha)
{
	if(texture){
		window_.drawSprite3D(texture, mvp, color.r, color.g, color.b, color.a * alpha);
	}
}

void UiContext::drawTexture(const std::shared_ptr<VulkanTexture> &texture, const LayoutRect &rect, const Color &color, float alpha)
{
	if(currentRotation_ != 0.0f){
		drawTextureRotated(texture, rect, currentRotation_, color, alpha);
	}
	else if(texture){
		window_.drawSprite2D(texture, rect.x, rect.y, rect.w, rect.h, color.r, color.g, color.b, color.a * alpha);
	}
}

void UiContext::drawTextureRotated(const std::shared_ptr<VulkanTexture> &texture, const LayoutRect &rect, float radians, const Color &color, float alpha)
{
	if(texture){
		window_.drawSprite2DRotated(texture, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, rect.w, rect.h, radians, color.r, color.g, color.b, color.a * alpha);
	}
}

} // namespace ui
