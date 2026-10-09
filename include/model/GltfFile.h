#if !defined(MODEL_GLTFFILE_H_)
#define MODEL_GLTFFILE_H_

#include "model/Json.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace model
{

// glTF 2.0 のファイルの中身(JSON と、バッファ0のバイト列)。モデル(GltfLoader)とアニメーション(AnimationClip)の読み込みが共通で使う。
// 形式は2通り: バイナリ(.glb/.vrm。JSON と BIN のチャンク)か、テキスト(.gltf。バッファ0は、外部ファイル(uri。.gltf からの相対パス)か、
// data: URI の base64)。読めなければ、コンストラクタが std::runtime_error を投げる(理由つき)
class GltfFile
{
public:
	explicit GltfFile(const std::string &fullPath);

	const JsonValue &json() const { return json_; }

	// accessor の中身を float に直したもの(要素数 × 成分数。正規化整数は、0〜1 / -1〜1 に直す)
	struct Accessor
	{
		std::vector<float> values;
		int components = 0;
		size_t count = 0;
	};
	// accessor を読む(sparse にも対応)。番号・範囲がおかしければ例外
	Accessor readAccessor(int index) const;
	// bufferView の生のバイト列。無ければ false
	bool viewBytes(int view, std::vector<uint8_t> &out) const;

	// ノードの親(無ければ -1)
	std::vector<int> nodeParents() const;
	// シーンのノードを、深さ優先で並べた順(親が必ず先。シーンの指定が無ければ、親の無いノード全部から)
	std::vector<int> nodeOrder(const std::vector<int> &parents) const;

	// ノードの、休止ポーズの局所の平行移動・回転(x,y,z,w)・拡大縮小(glTF の座標)。matrix で指定されたノードは、matrix に入れ、回転は単位とする(分解しない)
	struct NodeTransform
	{
		double translation[3] = {0, 0, 0};
		double rotation[4] = {0, 0, 0, 1};
		double scale[3] = {1, 1, 1};
		bool hasMatrix = false;
		double matrix[16] = {};
	};
	NodeTransform nodeTransform(size_t node) const;

private:
	struct SdlFree
	{
		void operator()(void *p) const;
	};
	using SdlBytes = std::unique_ptr<uint8_t, SdlFree>;
	static SdlBytes loadFile(const std::string &fullPath, size_t &size);
	void readView(int view, size_t byteOffset, int type, bool normalized, int components, size_t count, float *out) const;

	SdlBytes file_;               // ファイルの中身(.glb なら BIN チャンクも、この中を指す)
	SdlBytes external_;           // .gltf のバッファ0が外部ファイルなら、その中身
	std::vector<uint8_t> decoded_; // .gltf のバッファ0が data: URI なら、base64 を戻したもの
	const uint8_t *bin_ = nullptr;
	size_t binSize_ = 0;
	JsonValue json_;
};

// 座標系の変換(glTF の右手系 → PMX と同じ左手系): 各軸に符号を掛ける鏡像。VRM 0.x は正面が -Z なので X の鏡像、VRM 1.0 とただの glTF は Z の鏡像
struct GltfAxes
{
	double s[3];

	// json の拡張(VRM 0.x か)から決める
	static GltfAxes of(const JsonValue &json);
	void point(double *v) const { v[0] *= s[0]; v[1] *= s[1]; v[2] *= s[2]; }
	// 回転(x,y,z,w)を、鏡像の座標系での回転に直す(鏡像では回転軸=擬ベクトルの成分が、他の2軸の符号の積で変わる)
	void quaternion(double *q) const
	{
		const double x = q[0], y = q[1], z = q[2];
		q[0] = s[1] * s[2] * x;
		q[1] = s[0] * s[2] * y;
		q[2] = s[0] * s[1] * z;
	}
};

} // namespace model

#endif // MODEL_GLTFFILE_H_
