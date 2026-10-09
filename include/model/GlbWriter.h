#if !defined(MODEL_GLBWRITER_H_)
#define MODEL_GLBWRITER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model
{

// glTF 2.0 のバイナリ(.glb)を書く道具: データを1つのバッファ(BINチャンク)へ足していき、accessor・bufferView の JSON を貯める。
// 最上位の JSON は呼び出し側が組み立て、accessors() / views() / bin().size() を埋めて save() に渡す
class GlbWriter
{
public:
	// count 個の要素(1要素 components 個の成分。成分の型は glTF の componentType: 5123=UNSIGNED_SHORT、5125=UNSIGNED_INT、5126=FLOAT)の
	// バイト列 data を足し、accessor の番号を返す。extra は accessor に足す JSON の項目(",\"min\":[...]" など。空なら無し)
	int addAccessor(const void *data, size_t count, int componentType, int components, const char *type, const std::string &extra = std::string());
	// float の配列を足す。withMinMax なら、全部の値の最小・最大を min/max にする(1成分の、時刻の列などに使う)
	int addAccessor(const std::vector<float> &data, int components, const char *type, bool withMinMax);

	std::string views() const { return join(views_); }
	std::string accessors() const { return join(accessors_); }
	const std::vector<uint8_t> &bin() const { return bin_; }

	// json(最上位のオブジェクト)と、貯めたバイナリを、.glb にして fullPath へ書く。失敗したら false(理由は SDL_LogError に出す)
	bool save(const std::string &fullPath, std::string json) const;

	// JSON に書く数(float を、元へ戻せる桁数で)
	static std::string number(float v);

private:
	static std::string join(const std::vector<std::string> &items);
	std::vector<uint8_t> bin_;
	std::vector<std::string> views_;
	std::vector<std::string> accessors_;
};

} // namespace model

#endif // MODEL_GLBWRITER_H_
