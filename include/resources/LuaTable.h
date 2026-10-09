#if !defined(RESOURCES_LUATABLE_H_)
#define RESOURCES_LUATABLE_H_

#include <sol/sol.hpp>
#include <string>

// Luaのデータファイル(リポジトリ直下からの相対パス)を lua で実行し、グローバルの表 tableName を返す。
// 読めない・表が無いときは、logTag を付けてログに出し、空を返す。返した表は lua が生きている間だけ使える
sol::optional<sol::table> loadLuaTable(sol::state &lua, const std::string &relativePath, const char *tableName, const char *logTag);

// 表 table の項目 key を value に読む(表・項目が無ければ、value のまま = C++側の既定値)
template<typename T>
void readField(const sol::optional<sol::table> &table, const char *key, T &value)
{
	if(table){
		value = table->get_or(key, value);
	}
}

// 表 table の項目 key の配列({a, b, c} など)から、先頭の N 個を読む(無い要素は、そのまま)
template<size_t N>
void readArray(const sol::optional<sol::table> &table, const char *key, float (&values)[N])
{
	if(!table){
		return;
	}
	const sol::optional<sol::table> list = (*table)[key];
	if(!list){
		return;
	}
	for(size_t i = 0; i < N; ++i){
		values[i] = list->get_or(static_cast<int>(i + 1), values[i]);
	}
}

#endif // RESOURCES_LUATABLE_H_
