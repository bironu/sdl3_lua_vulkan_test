#if !defined(FIELD_FIELDMAP_H_)
#define FIELD_FIELDMAP_H_

#include <cstdint>
#include <string>
#include <vector>

namespace field
{

// 地面のタイルの種類(res/lua/data/field_tiles.lua で定義する。ゲームもエディタも、同じ定義を読む)
struct TileDef
{
	std::string id;      // 名前(例: "grass")
	char symbol = '.';   // フィールドのファイルで、このタイルを表す1文字
	uint8_t r = 128, g = 128, b = 128; // 色(テクスチャができるまでの代わり・ミニマップなど)
	std::string nameKey; // 表示名の、多言語のキー(lang/*.lua)
};

// フィールドの設定(res/lua/data/field_settings.lua): どのファイルを使うか。ゲームもエディタも、ここから読む
struct FieldSettings
{
	std::string tiles = "res/lua/data/field_tiles.lua";
	std::string field = "res/field/field01.lua";
};
// 設定を読む(読めない項目は既定のまま)
FieldSettings loadFieldSettings(const std::string &relativePath = "res/lua/data/field_settings.lua");

// タイルの種類の一覧を読む(失敗したら空)。パスはResourcePaths::resourceで解決する(リポジトリ直下からの相対パス)
std::vector<TileDef> loadTileDefs(const std::string &relativePath);

// 地面のタイルを、格子状に並べたもの(平らな地面。x: 0〜width-1、z: 0〜depth-1。1マスは cellSize メートル、原点は左上(x, zの最小側)の角)。
// ファイルはLuaの表(res/field/*.lua)で、1行が1つのzの列。人が読めて、差分も見やすい。タイルは番号(tiles定義の並び)で持つ
class FieldMap
{
public:
	FieldMap(int width = 32, int depth = 32, float cellSize = 1.0f);

	int width() const { return width_; }
	int depth() const { return depth_; }
	float cellSize() const { return cellSize_; }
	bool inside(int x, int z) const { return x >= 0 && z >= 0 && x < width_ && z < depth_; }
	// 範囲外は0
	uint8_t get(int x, int z) const { return inside(x, z) ? cells_[static_cast<size_t>(z) * width_ + x] : 0; }
	// 範囲外は何もしない。変わったらtrue
	bool set(int x, int z, uint8_t tile);
	void fill(uint8_t tile);

	// ファイルへ(絶対パス・または実行時のカレントからのパス)。tilesで、タイルの番号を1文字に直す。成功したらtrue
	bool save(const std::string &path, const std::vector<TileDef> &tiles) const;
	// ファイルから。tilesに無い文字は0番になる。成功したらtrue(失敗したら変わらない)
	bool load(const std::string &path, const std::vector<TileDef> &tiles);

private:
	int width_, depth_;
	float cellSize_;
	std::vector<uint8_t> cells_;
};

} // namespace field

#endif // FIELD_FIELDMAP_H_
