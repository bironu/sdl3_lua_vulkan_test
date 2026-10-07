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
	std::string id;      // 名前(例: "grass")。フィールドのファイルはこの名前でタイルを持つので、並びを変えても壊れない
	char symbol = '.';   // (旧形式の名残。いまは使わない)
	uint8_t r = 128, g = 128, b = 128; // 色(テクスチャができるまでの代わり・ミニマップなど)
	std::string nameKey; // 表示名の、多言語のキー(lang/*.lua)
	bool walkable = true; // 歩けるか(水など、歩けないタイルは false。FieldMovement が見る)
};

// フィールドの設定(res/lua/data/field_settings.lua): どのファイルを使うか。ゲームもエディタも、ここから読む
struct FieldSettings
{
	std::string tiles = "res/lua/data/field_tiles.lua";
	std::string field = "res/field/field01.fld";
	float maxSlope = 0.85f; // 歩いて登れる地面の勾配の上限(高さ/水平の距離。0.85で約40度)。これより急な所へは進めない
};
// 設定を読む(読めない項目は既定のまま)
FieldSettings loadFieldSettings(const std::string &relativePath = "res/lua/data/field_settings.lua");

// タイルの種類の一覧を読む(失敗したら空)。パスはResourcePaths::resourceで解決する(リポジトリ直下からの相対パス)
std::vector<TileDef> loadTileDefs(const std::string &relativePath);

// フィールドに置いた物(ビル・岩・木など)。モデルは res/prop/ の名前(FieldMap::propName)で指す
struct PlacedProp
{
	uint16_t prop = 0;  // FieldMap::propName の番号
	float x = 0.0f, y = 0.0f, z = 0.0f; // 位置(ワールド、メートル)。yは地面からの持ち上げ(0で地面に接する。地面の高さを変えても、物は地面に付いてくる)
	float yaw = 0.0f;   // Y軸まわりの回転(ラジアン)
	float scale = 1.0f; // 一様な拡大率
};

// フィールド: 地面のタイル(マスごと)・高さ(格子の頂点ごと)・置いた物。x: 0〜width-1、z: 0〜depth-1、1マスは cellSize メートル、原点は(x, zの最小側)の角。
// 高さは (width+1)*(depth+1) 個の頂点ごと(int16、1/kHeightScale メートル)。マスは、(x,z)-(x+1,z+1) の対角線で2枚の三角形に分ける
// (描画と、heightAt の補間が同じ分け方)。ファイルはバイナリ(res/field/*.fld)で、人が読める形ではない
class FieldMap
{
public:
	static constexpr int kHeightScale = 256; // 高さの整数値1メートルあたり

	FieldMap(int width = 32, int depth = 32, float cellSize = 1.0f);

	int width() const { return width_; }
	int depth() const { return depth_; }
	float cellSize() const { return cellSize_; }
	bool inside(int x, int z) const { return x >= 0 && z >= 0 && x < width_ && z < depth_; }

	// --- タイル(マスごと) ---
	// 範囲外は0
	uint8_t get(int x, int z) const { return inside(x, z) ? cells_[static_cast<size_t>(z) * width_ + x] : 0; }
	// 範囲外は何もしない。変わったらtrue
	bool set(int x, int z, uint8_t tile);
	void fill(uint8_t tile);

	// --- 高さ(頂点ごと。x: 0〜width、z: 0〜depth) ---
	int vertexCountX() const { return width_ + 1; }
	int vertexCountZ() const { return depth_ + 1; }
	bool insideVertex(int vx, int vz) const { return vx >= 0 && vz >= 0 && vx <= width_ && vz <= depth_; }
	int vertexIndex(int vx, int vz) const { return vz * (width_ + 1) + vx; }
	// 範囲外は端の頂点の値
	int16_t rawHeight(int vx, int vz) const;
	void setRawHeight(int index, int16_t value) { heights_[static_cast<size_t>(index)] = value; }
	int16_t rawHeightAt(int index) const { return heights_[static_cast<size_t>(index)]; }
	float vertexHeight(int vx, int vz) const { return static_cast<float>(rawHeight(vx, vz)) / kHeightScale; }
	// メートル。範囲外は何もしない。変わったらtrue
	bool setVertexHeight(int vx, int vz, float meters);
	void fillHeight(float meters);
	// ワールドの(x, z)の地面の高さ(マスの三角形の上で補間。範囲外は端の値)
	float heightAt(float x, float z) const;
	// ワールドの(x, z)の地面の法線(頂点の高さの差から求めた、なめらかなもの)
	void normalAt(float x, float z, float out[3]) const;
	// 頂点(vx, vz)の法線(中心差分)
	void vertexNormal(int vx, int vz, float out[3]) const;
	// 光線(origin + dir * t、t: 0〜maxDistance)と地面の交点。当たったらtrueで、tと交点を返す(フィールドの外では当たらない)
	bool raycast(const float origin[3], const float dir[3], float maxDistance, float &t, float hit[3]) const;

	// --- 置いた物 ---
	const std::vector<PlacedProp> &props() const { return props_; }
	// モデルの名前(res/prop/ の中のファイル名)→番号(無ければ足す)
	uint16_t propId(const std::string &name);
	const std::string &propName(uint16_t id) const { return propNames_[id]; }
	// 置く。liftは地面からの持ち上げ(メートル)。番号を返す
	size_t addProp(const std::string &name, float x, float z, float yaw, float scale, float lift = 0.0f);
	// 物の足元の、ワールドの高さ(地面の高さ + 持ち上げ)
	float propBaseY(const PlacedProp &prop) const { return heightAt(prop.x, prop.z) + prop.y; }
	// 元に戻すための、番号を指定しての挿入・削除
	void insertProp(size_t index, const PlacedProp &prop);
	PlacedProp removeProp(size_t index);

	// --- ファイル ---
	// ファイルへ(絶対パス・または実行時のカレントからのパス)。タイルは名前(TileDef::id)で持つ。成功したらtrue
	bool save(const std::string &path, const std::vector<TileDef> &tiles) const;
	// ファイルから。tilesに無いタイルは0番になる。成功したらtrue(失敗したら変わらない。無いのは普通なのでログは出さない)
	bool load(const std::string &path, const std::vector<TileDef> &tiles);

private:
	int width_, depth_;
	float cellSize_;
	std::vector<uint8_t> cells_;
	std::vector<int16_t> heights_;
	std::vector<std::string> propNames_;
	std::vector<PlacedProp> props_;
};

} // namespace field

#endif // FIELD_FIELDMAP_H_
