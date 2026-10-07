#if !defined(UI_WIDGETS_H_)
#define UI_WIDGETS_H_

#include "ui/Widget.h"
#include "vk/VulkanTexture.h"
#include <map>
#include <memory>
#include <string>

namespace ui
{

// 描画するものを持たない、子の取りまとめ用(位置・大きさ・不透明度・可視を子にまとめて効かせる)
class GroupWidget : public Widget
{
};

// 画像(Resourcesで読めるパス。例: "res/image/xxx.png")。大きさの既定は画像の大きさ。tintで色を掛ける
class ImageWidget : public Widget
{
public:
	void setImage(const std::string &path);
	const std::string &image() const { return path_; }
	void setTint(float r, float g, float b, float a = 1.0f) { tint_ = {r, g, b, a}; }

protected:
	bool intrinsicSize(UiContext &ctx, float &w, float &h) override;
	void drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha) override;

private:
	std::string path_;
	Color tint_;
};

// 文字(Resourcesのlua設定のフォント)。文字列・大きさが変わったときだけ、テクスチャを作り直す。大きさの既定は文字の大きさ
class TextWidget : public Widget
{
public:
	// 文字列を直接指定する(キーの指定は解除される)
	void setText(const std::string &text);
	const std::string &text() const { return text_; }
	// 文字列を、多言語のキー(lang/*.luaのstrings)で指定する。言語が切り替わると、自動で更新される。varsは{名前}の差し込み
	void setTextKey(const std::string &key);
	void setTextVars(const std::map<std::string, std::string> &vars);
	const std::string &textKey() const { return key_; }
	// フォントの名前(lang/*.luaのfonts。無ければdefault)と、大きさ(0以下ならそのフォントの既定)
	void setFont(const std::string &name);
	void setFontSize(float size);
	void setColor(float r, float g, float b, float a = 1.0f) { color_ = {r, g, b, a}; }
	// ビットマップフォントで描く(ASCIIだけ。文字列が毎フレーム変わっても、テクスチャを作り直さない。FPS・座標・カウンタ向き)。回転(angle)は効かない
	void setBitmap(bool bitmap) { bitmap_ = bitmap; }
	bool bitmap() const { return bitmap_; }
	// 縁取り: 文字を、widthピクセル(論理画面)だけ8方向にずらした同じ文字(outlineの色)を、文字の下に重ねて描く。0で縁取りなし。矩形の大きさには含めない
	void setOutline(float width, float r, float g, float b, float a = 1.0f) { outlineWidth_ = width; outlineColor_ = {r, g, b, a}; }

protected:
	bool intrinsicSize(UiContext &ctx, float &w, float &h) override;
	void drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha) override;

private:
	void ensureTexture(UiContext &ctx);
	void resolveText(UiContext &ctx); // キー指定なら、言語が変わったときに文字列を解決し直す
	std::string text_;
	std::string key_;
	std::map<std::string, std::string> vars_;
	uint32_t resolvedVersion_ = 0; // keyを解決したときの言語の版(Resources::languageVersion)。0は未解決
	std::string fontName_ = "default";
	float fontSize_ = 0.0f;
	Color color_;
	bool bitmap_ = false;
	float outlineWidth_ = 0.0f;
	Color outlineColor_{0.0f, 0.0f, 0.0f, 1.0f};
	std::shared_ptr<VulkanTexture> texture_;
	uint32_t fontVersion_ = 0; // フォントを作ったときの言語の版
	bool dirty_ = true;
};

// 画面と同じ大きさの背景(単色、または画像を画面いっぱいに伸ばす)。位置・大きさは変えられない(常に画面全体)
class BackgroundWidget : public Widget
{
public:
	void setColor(float r, float g, float b, float a = 1.0f) { color_ = {r, g, b, a}; }
	void setImage(const std::string &path) { path_ = path; }

protected:
	bool intrinsicSize(UiContext &ctx, float &w, float &h) override;
	void drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha) override;

private:
	std::string path_;
	Color color_{0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace ui

#endif // UI_WIDGETS_H_
