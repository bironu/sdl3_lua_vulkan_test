#if !defined(RESOURCES_LUATABLE_H_)
#define RESOURCES_LUATABLE_H_

#include <sol/sol.hpp>
#include <string>

// Luaのデータファイル(リポジトリ直下からの相対パス)を lua で実行し、グローバルの表 tableName を返す。
// 読めない・表が無いときは、logTag を付けてログに出し、空を返す。返した表は lua が生きている間だけ使える
sol::optional<sol::table> loadLuaTable(sol::state &lua, const std::string &relativePath, const char *tableName, const char *logTag);

#endif // RESOURCES_LUATABLE_H_
