#if !defined(MODEL_JSON_H_)
#define MODEL_JSON_H_

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace model
{

// 最小限のJSONの値(glTF/VRMのJSONチャンクを読むための小さなパーサー用)。
// 存在しないキー・範囲外の添字は、nullの値を返す(つないで辿っても落ちない)。数値はすべてdouble
class JsonValue
{
public:
	enum class Type { Null, Bool, Number, String, Array, Object };

	Type type = Type::Null;
	bool boolean = false;
	double number = 0.0;
	std::string string;
	std::vector<JsonValue> array;
	std::vector<std::pair<std::string, JsonValue>> object; // 並びは入力のまま

	bool isNull() const { return type == Type::Null; }
	bool isArray() const { return type == Type::Array; }
	bool isObject() const { return type == Type::Object; }
	bool isString() const { return type == Type::String; }
	bool isNumber() const { return type == Type::Number; }
	size_t size() const { return type == Type::Array ? array.size() : (type == Type::Object ? object.size() : 0); }

	const JsonValue &operator[](const char *key) const;
	const JsonValue &operator[](size_t index) const;

	double asNumber(double fallback = 0.0) const { return type == Type::Number ? number : fallback; }
	int asInt(int fallback = 0) const { return type == Type::Number ? static_cast<int>(number) : fallback; }
	bool asBool(bool fallback = false) const { return type == Type::Bool ? boolean : fallback; }
	std::string asString(const std::string &fallback = std::string()) const { return type == Type::String ? string : fallback; }
};

// textをJSONとして解釈する。成功したらtrue(失敗時のoutは不定。errorに理由)
bool parseJson(const char *text, size_t length, JsonValue &out, std::string &error);

} // namespace model

#endif // MODEL_JSON_H_
