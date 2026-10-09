#if !defined(CREATURE_CREATUREDEFINITION_H_)
#define CREATURE_CREATUREDEFINITION_H_

#include "creature/CreatureAnimator.h"
#include "creature/CreatureBuilder.h"
#include <optional>
#include <string>

namespace creature
{

// ツール creature2glb が作る、1種類の丸い生き物の定義(tools/creatures/*.lua の creature の表): 形(CreatureSpec)と、焼き込む動き(CreatureMotion)
struct CreatureDefinition
{
	std::string name = "creature"; // メッシュ・スキン・シーンの名前
	float length = 1.0f;           // 体長(m)。形の前後の長さを、これに合わせて拡大縮小して書き出す(形の長さの単位は任意)
	model::CreatureSpec spec;
	game::CreatureMotion motion;
};

// relativePath(リポジトリ直下からの相対パス)の Lua の creature の表を読む(無い項目は、C++側の既定値)。読めなければ空(理由はログに出す)
std::optional<CreatureDefinition> loadCreatureDefinition(const std::string &relativePath);

} // namespace creature

#endif // CREATURE_CREATUREDEFINITION_H_
