#if !defined(RESOURCES_ASSETCACHE_H_)
#define RESOURCES_ASSETCACHE_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// 同じキー(パス+読み方の違い)のデータを、1つだけ作って共有するキャッシュ。
// 弱参照(weak_ptr)で持つので、使っている人(shared_ptrを持つ人)が全員手放したら、自動で解放される。
// 手動のunloadも参照カウントも要らない: 他のシーンが使っているものは消えず、誰も使わなくなったものは消える。
// スレッドセーフ(別スレッドからacquireしてよい)。読み込み自体はロックの外で行うので、他のキーの取得を止めない
// (同じキーを同時に読んだときは、片方の結果を捨てて、先に登録された方を共有する)
template<typename T>
class AssetCache
{
public:
	using Loader = std::function<std::shared_ptr<T>()>;

	// キーのデータを返す。無ければloaderで読む(失敗したらnullptrを返し、キャッシュしない)
	std::shared_ptr<T> acquire(const std::string &key, const Loader &loader)
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			const auto found = entries_.find(key);
			if(found != entries_.end()){
				if(auto alive = found->second.lock()){
					++hits_;
					return alive;
				}
			}
		}
		std::shared_ptr<T> loaded = loader();
		if(!loaded){
			return nullptr;
		}
		std::lock_guard<std::mutex> lock(mutex_);
		auto &slot = entries_[key];
		if(auto alive = slot.lock()){
			++hits_; // 他のスレッドが先に登録していた
			return alive;
		}
		slot = loaded;
		++misses_;
		return loaded;
	}

	// 解放済みのエントリを消す
	void purgeExpired()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(auto it = entries_.begin(); it != entries_.end();){
			it = it->second.expired() ? entries_.erase(it) : std::next(it);
		}
	}

	// まだ誰かが持っているエントリの(キー, 持っている数)。アプリ終了時の、解放漏れの点検に使う
	std::vector<std::pair<std::string, long>> aliveEntries() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<std::pair<std::string, long>> result;
		for(const auto &entry : entries_){
			if(const long count = entry.second.use_count(); count > 0){
				result.emplace_back(entry.first, count);
			}
		}
		return result;
	}

	size_t hits() const { std::lock_guard<std::mutex> lock(mutex_); return hits_; }
	size_t misses() const { std::lock_guard<std::mutex> lock(mutex_); return misses_; }

private:
	mutable std::mutex mutex_;
	std::unordered_map<std::string, std::weak_ptr<T>> entries_;
	size_t hits_ = 0;   // キャッシュにあった回数
	size_t misses_ = 0; // 読み込んだ回数
};

#endif // RESOURCES_ASSETCACHE_H_
