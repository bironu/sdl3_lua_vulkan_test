#include "model/VmdLoader.h"
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>
#include <iconv.h>
#include <stdexcept>
#include <unordered_map>

namespace model
{

namespace
{

class Reader
{
public:
	Reader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

	template<typename T>
	T read()
	{
		T value;
		require(sizeof(T));
		std::memcpy(&value, data_ + pos_, sizeof(T));
		pos_ += sizeof(T);
		return value;
	}
	const uint8_t *bytes(size_t n)
	{
		require(n);
		const uint8_t *p = data_ + pos_;
		pos_ += n;
		return p;
	}
	void skip(size_t n) { bytes(n); }
	bool atEnd() const { return pos_ >= size_; }
	size_t remaining() const { return size_ - pos_; }

private:
	void require(size_t n) const
	{
		if(pos_ + n > size_){
			throw std::runtime_error("unexpected end of file");
		}
	}
	const uint8_t *data_;
	size_t size_;
	size_t pos_ = 0;
};

// 固定長(NUL終端/NUL埋め)のShift-JIS(CP932)文字列をUTF-8にする。
// (SDL_iconvは、このSDLのビルドではShift-JISを扱えないので、OS標準のiconvを使う)
// ボーン名はキーフレームごとに同じものが何度も出てくるので、変換結果を覚えておく
class SjisConverter
{
public:
	SjisConverter()
		: cd_(iconv_open("UTF-8", "CP932"))
	{
		if(cd_ == reinterpret_cast<iconv_t>(-1)){
			cd_ = iconv_open("UTF-8", "SHIFT_JIS");
		}
	}
	~SjisConverter()
	{
		if(cd_ != reinterpret_cast<iconv_t>(-1)){
			iconv_close(cd_);
		}
	}
	SjisConverter(const SjisConverter &) = delete;
	SjisConverter &operator=(const SjisConverter &) = delete;

	std::string convert(const uint8_t *bytes, size_t maxLength)
	{
		size_t length = 0;
		while(length < maxLength && bytes[length] != 0){
			++length;
		}
		if(length == 0){
			return {};
		}
		const std::string key(reinterpret_cast<const char *>(bytes), length);
		const auto cached = cache_.find(key);
		if(cached != cache_.end()){
			return cached->second;
		}
		std::string result = key; // 変換できなければそのまま(ASCII名なら正しい)
		if(cd_ != reinterpret_cast<iconv_t>(-1)){
			char out[256];
			char *in = const_cast<char *>(key.data());
			size_t inLeft = key.size();
			char *outPtr = out;
			size_t outLeft = sizeof(out);
			iconv(cd_, nullptr, nullptr, nullptr, nullptr); // 状態をリセット
			if(iconv(cd_, &in, &inLeft, &outPtr, &outLeft) != static_cast<size_t>(-1)){
				result.assign(out, sizeof(out) - outLeft);
			}
		}
		cache_.emplace(key, result);
		return result;
	}

private:
	iconv_t cd_;
	std::unordered_map<std::string, std::string> cache_;
};

} // namespace

std::shared_ptr<Motion> loadVmd(const std::string &fullPath)
{
	size_t fileSize = 0;
	void *fileData = SDL_LoadFile(fullPath.c_str(), &fileSize);
	if(!fileData){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VMD open error. %s (%s)", SDL_GetError(), fullPath.c_str());
		return nullptr;
	}

	auto motion = std::make_shared<Motion>();
	try{
		Reader r(static_cast<const uint8_t *>(fileData), fileSize);

		// ヘッダー: 30バイトの識別子 + 20バイトのモデル名(バージョン2)
		const uint8_t *magic = r.bytes(30);
		if(std::memcmp(magic, "Vocaloid Motion Data", 20) != 0){
			throw std::runtime_error("not a VMD file");
		}
		const bool version2 = std::memcmp(magic, "Vocaloid Motion Data 0002", 25) == 0;
		SjisConverter sjis;
		motion->modelName = sjis.convert(r.bytes(version2 ? 20 : 10), version2 ? 20 : 10);

		// ボーンのキーフレーム(1件111バイト)。同じボーンのものをトラックにまとめる
		const uint32_t boneFrameCount = r.read<uint32_t>();
		if(boneFrameCount > 5000000){
			throw std::runtime_error("bad bone frame count");
		}
		std::vector<std::pair<std::string, size_t>> trackIndex;
		for(uint32_t i = 0; i < boneFrameCount; ++i){
			const std::string name = sjis.convert(r.bytes(15), 15);
			MotionKey key;
			key.frame = r.read<uint32_t>();
			key.translation.x = r.read<float>();
			key.translation.y = r.read<float>();
			key.translation.z = r.read<float>();
			key.rotation.x = r.read<float>();
			key.rotation.y = r.read<float>();
			key.rotation.z = r.read<float>();
			key.rotation.w = r.read<float>();
			key.rotation = key.rotation.normalized();
			const uint8_t *ip = r.bytes(64);
			// 64バイトの中の曲線の並び: 移動X, 移動Y, 移動Z, 回転が16バイトずつ。各曲線は 4バイトおきに x1,y1,x2,y2
			for(int curve = 0; curve < 4; ++curve){
				for(int k = 0; k < 4; ++k){
					key.interpolation[curve][k] = ip[curve * 16 + k * 4];
				}
			}
			motion->lastFrame = std::max(motion->lastFrame, key.frame);

			auto found = std::find_if(trackIndex.begin(), trackIndex.end(), [&](const auto &p){ return p.first == name; });
			if(found == trackIndex.end()){
				trackIndex.emplace_back(name, motion->tracks.size());
				motion->tracks.push_back({name, {}});
				found = trackIndex.end() - 1;
			}
			motion->tracks[found->second].keys.push_back(key);
		}
		for(auto &track : motion->tracks){
			std::stable_sort(track.keys.begin(), track.keys.end(), [](const MotionKey &a, const MotionKey &b){ return a.frame < b.frame; });
		}

		// ここから先(モーフ・カメラ・照明・影・IK)は、無い(ファイルが終わる)ことがある
		if(r.remaining() >= 4){
			// モーフ: 名前15バイト、フレーム、重み(float)の23バイト
			const uint32_t morphFrameCount = r.read<uint32_t>();
			if(morphFrameCount > 5000000){
				throw std::runtime_error("bad morph frame count");
			}
			std::vector<std::pair<std::string, size_t>> morphIndex;
			for(uint32_t i = 0; i < morphFrameCount; ++i){
				const std::string name = sjis.convert(r.bytes(15), 15);
				MorphKey key;
				key.frame = r.read<uint32_t>();
				key.weight = r.read<float>();
				motion->lastFrame = std::max(motion->lastFrame, key.frame);
				auto found = std::find_if(morphIndex.begin(), morphIndex.end(), [&](const auto &p){ return p.first == name; });
				if(found == morphIndex.end()){
					morphIndex.emplace_back(name, motion->morphTracks.size());
					motion->morphTracks.push_back({name, {}});
					found = morphIndex.end() - 1;
				}
				motion->morphTracks[found->second].keys.push_back(key);
			}
			for(auto &track : motion->morphTracks){
				std::stable_sort(track.keys.begin(), track.keys.end(), [](const MorphKey &a, const MorphKey &b){ return a.frame < b.frame; });
			}
		}
		if(r.remaining() >= 4){
			r.skip(static_cast<size_t>(r.read<uint32_t>()) * 61); // カメラ
		}
		if(r.remaining() >= 4){
			r.skip(static_cast<size_t>(r.read<uint32_t>()) * 28); // 照明
		}
		if(r.remaining() >= 4){
			r.skip(static_cast<size_t>(r.read<uint32_t>()) * 9);  // セルフシャドウ
		}
		if(r.remaining() >= 4){
			// IKのオン/オフ: フレーム、表示フラグ、IK数、(名前20バイト + オン/オフ1バイト) x IK数
			const uint32_t ikFrameCount = r.read<uint32_t>();
			for(uint32_t i = 0; i < ikFrameCount; ++i){
				IkSwitchKey key;
				key.frame = r.read<uint32_t>();
				r.skip(1); // 表示フラグ
				const uint32_t count = r.read<uint32_t>();
				for(uint32_t j = 0; j < count; ++j){
					const std::string name = sjis.convert(r.bytes(20), 20);
					const bool on = r.read<uint8_t>() != 0;
					key.states.emplace_back(name, on);
				}
				motion->ikSwitches.push_back(std::move(key));
			}
			std::stable_sort(motion->ikSwitches.begin(), motion->ikSwitches.end(),
				[](const IkSwitchKey &a, const IkSwitchKey &b){ return a.frame < b.frame; });
		}
	}
	catch(const std::exception &e){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "VMD parse error: %s (%s)", e.what(), fullPath.c_str());
		motion = nullptr;
	}
	SDL_free(fileData);
	return motion;
}

} // namespace model
