#if !defined(UI_UICONTEXT_H_)
#define UI_UICONTEXT_H_

#include "geo/Matrix.h"
#include "ui/Widget.h"
#include "vk/VulkanTexture.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

class Resources;
class ResourceSet;
namespace SDL_
{
class VulkanWindow;
class TtfFont;
class BitmapFont;
}

namespace ui
{

// ゲームの状態のうち、HUD(ミニマップ・数値表示)が見るもの。ゲームのシーンが毎フレーム書き込む。座標はワールド(メートル。x右・z手前)
struct WorldView
{
	struct Marker
	{
		float x = 0.0f, z = 0.0f;
		Color color;
	};
	float playerX = 0.0f, playerZ = 0.0f;
	float heading = 0.0f;   // プレイヤーの向き(Y軸まわり。+Zが0、atan2(x, z))
	float cameraYaw = 0.0f; // カメラの水平角(GameSceneのcameraYaw_と同じ)
	// 動ける範囲(長方形)。幅・奥行きが0なら、境界なし
	float boundsMinX = 0.0f, boundsMinZ = 0.0f, boundsMaxX = 0.0f, boundsMaxZ = 0.0f;
	float fps = 0.0f;
	float sensitivity = 1.0f; // 視点の回転の感度の倍率(ポーズ画面のオプション)
	std::vector<Marker> markers; // ミニマップに出す点(敵など。プレイヤーは含まない)
	std::map<std::string, std::string> strings; // 文字列の値(Luaの world.strings.名前 で読める。エディタの置物の名前など)
	std::map<std::string, float> values; // その他の、シーンごとの数値(Luaの world.名前 で読める。エディタの選択中のタイルなど)
};

// ウィジェットの描画に要るもの(ウィンドウ・リソース・画面の大きさ)と、テクスチャのキャッシュ
class UiContext
{
public:
	// resourceSet: 読んだ画像を持っておく入れ物(シーンのもの。シーンが終わると手放される)
	UiContext(SDL_::VulkanWindow &window, Resources &res, ResourceSet &resourceSet);
	~UiContext();

	float screenWidth() const { return screenWidth_; }
	float screenHeight() const { return screenHeight_; }
	SDL_::VulkanWindow &window() { return window_; }

	// パスの画像のテクスチャ(キャッシュする。読めなければnullptr。読めなかったことも覚えて、ログは1回だけ)
	std::shared_ptr<VulkanTexture> image(const std::string &path);
	// 1x1の白(単色の面を、色を掛けて描くのに使う)
	const std::shared_ptr<VulkanTexture> &white() const { return white_; }
	// 文字列のテクスチャ(白で描く。色は描画時に掛ける)。fontNameはlang/*.luaのfontsの名前(無ければdefault)、fontSizeが0以下ならそのフォントの既定の大きさ。
	// フォント(TtfFont)は、ファイル+大きさでキャッシュする。作れなければnullptr
	std::shared_ptr<VulkanTexture> renderText(const std::string &text, const std::string &fontName, float fontSize);
	// ビットマップフォント: ASCII(0x20-0x7E)の全文字を、1枚のテクスチャに並べたもの(ファイル+大きさでキャッシュ)。文字列が変わっても、
	// テクスチャを作り直さずに、1文字ずつ描ける(FPS・座標のように毎フレーム変わる数字向き)。ASCII以外の文字は空白になる。作れなければnullptr
	struct BitmapGlyphs
	{
		std::unique_ptr<SDL_::BitmapFont> font;
		std::shared_ptr<VulkanTexture> texture;
		float lineHeight() const;
		float textWidth(const std::string &text) const;
	};
	const BitmapGlyphs *bitmapFont(const std::string &fontName, float fontSize);
	// 文字列を、(x, y)(左上)から、scale倍で描く(描画の予約)。色・アルファは文字の色
	void drawBitmapText(const BitmapGlyphs &glyphs, const std::string &text, float x, float y, float scale, const Color &color, float alpha);
	// 多言語(Resourcesのもの): キーの文字列を現在の言語で。言語・定義が変わるたびにlanguageVersionが増える
	std::string translate(const std::string &key, const std::map<std::string, std::string> &vars) const;
	uint32_t languageVersion() const;
	Resources &resources() { return res_; }
	WorldView &world() { return world_; }

	// 3D空間のスプライト(中心原点の1x1の板に、mvpを掛ける。奥から順に呼ぶこと)
	void drawTexture3D(const std::shared_ptr<VulkanTexture> &texture, const geo::Matrix4x4f &mvp, const Color &color, float alpha);
	// テクスチャを、論理画面の矩形へ、色(各0〜1)とアルファを掛けて描く(描画の予約)
	// (Widget::drawが設定している回転があれば、矩形の中心のまわりに回して描く)
	void drawTexture(const std::shared_ptr<VulkanTexture> &texture, const LayoutRect &rect, const Color &color, float alpha);
	// 矩形の中心のまわりに、radians(時計回りが正)回して描く(ミニマップの線など)
	void drawTextureRotated(const std::shared_ptr<VulkanTexture> &texture, const LayoutRect &rect, float radians, const Color &color, float alpha);
	// 次のdrawTextureから掛ける回転(Widget::drawが、自分のdrawSelfの間だけ設定する)
	void setCurrentRotation(float radians) { currentRotation_ = radians; }

private:
	SDL_::VulkanWindow &window_;
	Resources &res_;
	ResourceSet &resourceSet_;
	float screenWidth_;
	float screenHeight_;
	std::map<std::string, std::shared_ptr<VulkanTexture>> images_;
	std::map<std::string, std::unique_ptr<SDL_::TtfFont>> fonts_; // キー: ファイル@大きさ
	std::map<std::string, std::unique_ptr<BitmapGlyphs>> bitmapFonts_; // 同上
	std::shared_ptr<VulkanTexture> white_;
	WorldView world_;
	float currentRotation_ = 0.0f;
};

} // namespace ui

#endif // UI_UICONTEXT_H_
