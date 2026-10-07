#if !defined(UI_MENUWIDGETS_H_)
#define UI_MENUWIDGETS_H_

#include "ui/Widget.h"
#include <string>
#include <vector>

namespace ui
{

// 3D空間で、画像のアイコンが円状に並んで回るメニュー(回転メニュー)。画面の矩形の中に、透視投影で描く。
// カメラは原点で-Z向き、円はその前方(ringDistance)に、X軸まわりに傾けて置く。正面のアイコンが選択中で、手前に出て不透明になる。
// 回転は "rotation"(度)プロパティで、animateできる。回転・選択は、rotate()/select()で(アニメーションつきで)行う。
// 項目(画像のパス)・見た目のパラメータは、Luaから設定する。入力の処理(どのキーで回すか)はスクリプト側の仕事
class CarouselWidget : public Widget
{
public:
	void setItems(const std::vector<std::string> &imagePaths) { items_ = imagePaths; }
	size_t itemCount() const { return items_.size(); }
	// 円の半径、傾き(度。手前側へ傾けると上から見下ろす形になる)、視野角(度)、カメラから円の中心までの距離
	void setRadius(float radius) { radius_ = radius; }
	void setTilt(float degrees) { tiltDegrees_ = degrees; }
	void setViewAngle(float degrees) { viewAngle_ = degrees; }
	void setRingDistance(float distance) { ringDistance_ = distance; }
	// アイコンの大きさ(3D空間の単位)
	void setItemSize(float w, float h) { itemWidth_ = w; itemHeight_ = h; }
	// 通常/選択中のアイコンの不透明度(0〜1)と、選択中が手前へ出る量
	void setItemAlpha(float normal, float selected) { alphaNormal_ = normal; alphaSelected_ = selected; }
	void setSelectedOffset(float offset) { selectedOffset_ = offset; }
	// trueなら、アイコンを常にカメラへ向ける。falseなら、円の外側へ向ける
	void setBillboard(bool billboard) { billboard_ = billboard; }

	// 選択中の項目の番号(0始まり)。項目が無ければ-1
	int selected() const;
	// 選択を、deltaだけ(正で次へ)動かす。durationSeconds秒かけて回る(0ですぐ)。連続して呼ぶと、目標が足し合わさる
	void rotate(int delta, float durationSeconds);
	// 選択を、番号へ。近い向きに回る
	void select(int index, float durationSeconds);

	bool getProperty(const std::string &name, float &value) const override;
	bool setProperty(const std::string &name, float value) override;

protected:
	void drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha) override;

private:
	std::vector<std::string> items_;
	float radius_ = 20.0f;
	float tiltDegrees_ = 13.0f;
	float viewAngle_ = 45.0f;
	float ringDistance_ = 40.0f;
	float itemWidth_ = 4.0f;
	float itemHeight_ = 3.0f;
	float alphaNormal_ = 160.0f / 255.0f;
	float alphaSelected_ = 1.0f;
	float selectedOffset_ = 10.0f;
	bool billboard_ = true;
	float rotation_ = 0.0f;       // 現在の回転(度)。回り続けても360度に戻さない
	float targetRotation_ = 0.0f; // 回転の目標(連続したrotate()の足し合わせ用)
};

// 横に並んだ画像のアイコンの帯(オプションバー)。背景の半透明の帯の上に、選択中のアイコンが矩形の中央に来るよう、横へスクロールする。
// スクロールは "scroll"(ピクセル)プロパティで、animateできる。select()でアニメーションつきに選択を動かす
class StripWidget : public Widget
{
public:
	void setItems(const std::vector<std::string> &imagePaths) { items_ = imagePaths; }
	size_t itemCount() const { return items_.size(); }
	void setItemSize(float w, float h) { itemWidth_ = w; itemHeight_ = h; }
	// アイコン同士の隙間(ピクセル)
	void setGap(float gap) { gap_ = gap; }
	// 帯の背景の色(0〜1。アルファが0なら描かない)
	void setBackground(float r, float g, float b, float a) { background_ = {r, g, b, a}; }
	// 背景のアルファだけを変える(カーソルが入った/出たときの強調など)
	void setBackgroundAlpha(float a) { background_.a = a; }

	int selected() const { return selected_; }
	void select(int index, float durationSeconds);

	bool getProperty(const std::string &name, float &value) const override;
	bool setProperty(const std::string &name, float value) override;

protected:
	void drawSelf(UiContext &ctx, const LayoutRect &rect, float alpha) override;

private:
	std::vector<std::string> items_;
	float itemWidth_ = 120.0f;
	float itemHeight_ = 90.0f;
	float gap_ = 12.0f;
	Color background_{0.0f, 0.0f, 0.0f, 0.25f};
	int selected_ = 0;
	float scroll_ = 0.0f;
};

} // namespace ui

#endif // UI_MENUWIDGETS_H_
