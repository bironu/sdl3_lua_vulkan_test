#if !defined(RANDOM_H_)
#define RANDOM_H_

#include <cstdlib>

// 0以上n未満の擬似乱数(旧xanadu.hのrandom_integer()マクロ)
inline int randomInteger(int n)
{
	return rand() % n;
}

// 0以上10未満の擬似乱数。移動方向(1-9のテンキー配置+0)の選択に使う
// (旧xanadu.hのrandom_direction()マクロ)
inline int randomDirection()
{
	return randomInteger(10);
}

#endif // RANDOM_H_
