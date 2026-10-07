#include "model/FbxLoader.h"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace model
{

namespace
{

struct FbxProp
{
	char type = 0;
	int64_t i = 0;
	double d = 0.0;
	std::string s;
	std::vector<double> numbers; // 配列(f,d,l,i,b)は、すべてdoubleにして持つ
};

struct FbxNode
{
	std::string name;
	std::vector<FbxProp> props;
	std::vector<FbxNode> children;

	const FbxNode *find(const char *childName) const
	{
		for(const auto &c : children){
			if(c.name == childName){ return &c; }
		}
		return nullptr;
	}
};

class Reader
{
public:
	Reader(const uint8_t *data, size_t size, uint32_t version) : data_(data), size_(size), version_(version) {}

	// 位置posのノードを読む。終端のノード(全部0)ならfalse
	bool readNode(size_t &pos, FbxNode &out, int depth = 0)
	{
		if(depth > 64){ throw std::runtime_error("nesting too deep"); }
		uint64_t end, count, listLength;
		size_t headerSize;
		if(version_ >= 7500){
			need(pos, 25);
			std::memcpy(&end, data_ + pos, 8);
			std::memcpy(&count, data_ + pos + 8, 8);
			std::memcpy(&listLength, data_ + pos + 16, 8);
			headerSize = 24;
		}
		else{
			need(pos, 13);
			uint32_t e, c, l;
			std::memcpy(&e, data_ + pos, 4);
			std::memcpy(&c, data_ + pos + 4, 4);
			std::memcpy(&l, data_ + pos + 8, 4);
			end = e; count = c; listLength = l;
			headerSize = 12;
		}
		const uint8_t nameLength = data_[pos + headerSize];
		if(end == 0){
			pos += headerSize + 1;
			return false;
		}
		size_t p = pos + headerSize + 1;
		need(p, nameLength);
		out.name.assign(reinterpret_cast<const char *>(data_ + p), nameLength);
		p += nameLength;
		(void)listLength;
		if(end > size_){ throw std::runtime_error("node out of range"); }
		for(uint64_t k = 0; k < count; ++k){
			out.props.push_back(readProp(p));
		}
		while(p < end){
			FbxNode child;
			if(!readNode(p, child, depth + 1)){
				break;
			}
			out.children.push_back(std::move(child));
		}
		pos = static_cast<size_t>(end);
		return true;
	}

private:
	void need(size_t pos, size_t n) const
	{
		if(pos + n > size_){ throw std::runtime_error("unexpected end of file"); }
	}

	template<typename T>
	T read(size_t &p)
	{
		need(p, sizeof(T));
		T v;
		std::memcpy(&v, data_ + p, sizeof(T));
		p += sizeof(T);
		return v;
	}

	FbxProp readProp(size_t &p)
	{
		FbxProp prop;
		prop.type = static_cast<char>(read<uint8_t>(p));
		switch(prop.type){
		case 'Y': prop.i = read<int16_t>(p); break;
		case 'C': prop.i = read<uint8_t>(p); break;
		case 'I': prop.i = read<int32_t>(p); break;
		case 'L': prop.i = read<int64_t>(p); break;
		case 'F': prop.d = read<float>(p); prop.i = static_cast<int64_t>(prop.d); break;
		case 'D': prop.d = read<double>(p); prop.i = static_cast<int64_t>(prop.d); break;
		case 'S': case 'R':{
			const uint32_t n = read<uint32_t>(p);
			need(p, n);
			prop.s.assign(reinterpret_cast<const char *>(data_ + p), n);
			p += n;
			break;
		}
		case 'f': case 'd': case 'l': case 'i': case 'b':{
			const uint32_t count = read<uint32_t>(p);
			const uint32_t encoding = read<uint32_t>(p);
			const uint32_t compressed = read<uint32_t>(p);
			need(p, compressed);
			const size_t elementSize = prop.type == 'f' || prop.type == 'i' ? 4 : (prop.type == 'b' ? 1 : 8);
			std::vector<uint8_t> raw;
			if(encoding == 1){
				raw.resize(static_cast<size_t>(count) * elementSize);
				uLongf outSize = static_cast<uLongf>(raw.size());
				if(uncompress(raw.data(), &outSize, data_ + p, compressed) != Z_OK || outSize != raw.size()){
					throw std::runtime_error("zlib inflate failed");
				}
			}
			else{
				if(compressed != static_cast<size_t>(count) * elementSize){ throw std::runtime_error("bad array size"); }
				raw.assign(data_ + p, data_ + p + compressed);
			}
			p += compressed;
			prop.numbers.resize(count);
			for(uint32_t k = 0; k < count; ++k){
				const uint8_t *e = raw.data() + static_cast<size_t>(k) * elementSize;
				switch(prop.type){
				case 'f':{ float v; std::memcpy(&v, e, 4); prop.numbers[k] = v; break; }
				case 'd':{ double v; std::memcpy(&v, e, 8); prop.numbers[k] = v; break; }
				case 'l':{ int64_t v; std::memcpy(&v, e, 8); prop.numbers[k] = static_cast<double>(v); break; }
				case 'i':{ int32_t v; std::memcpy(&v, e, 4); prop.numbers[k] = v; break; }
				default: prop.numbers[k] = *e; break;
				}
			}
			break;
		}
		default: throw std::runtime_error(std::string("unknown property type ") + prop.type);
		}
		return prop;
	}

	const uint8_t *data_;
	size_t size_;
	uint32_t version_;
};

// ---- 回転 ----

struct Q
{
	double x = 0, y = 0, z = 0, w = 1;
};

Q mul(const Q &a, const Q &b)
{
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Q conj(const Q &q) { return {-q.x, -q.y, -q.z, q.w}; }

Q normalize(const Q &q)
{
	const double len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	return len > 1e-12 ? Q{q.x / len, q.y / len, q.z / len, q.w / len} : Q{};
}

Q axisAngle(int axis, double radians)
{
	Q q;
	const double s = std::sin(radians * 0.5);
	q.w = std::cos(radians * 0.5);
	(axis == 0 ? q.x : (axis == 1 ? q.y : q.z)) = s;
	return q;
}

Q rotate(const Q &a, const double v[3])
{
	// v' = a * v * a^-1
	const Q p = {v[0], v[1], v[2], 0.0};
	const Q r = mul(mul(a, p), conj(a));
	return r;
}

// FBXのオイラー角(度)。回転の順序order(0=XYZ: Xを最初に回す → R = Rz*Ry*Rx)
Q eulerToQuat(const double e[3], int order)
{
	const double k = 3.14159265358979323846 / 180.0;
	const Q q[3] = {axisAngle(0, e[0] * k), axisAngle(1, e[1] * k), axisAngle(2, e[2] * k)};
	// order(FBXのeEulerXYZ=0, XZY=1, YZX=2, YXZ=3, ZXY=4, ZYX=5): 先に回す軸が右側
	static const int sequence[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 2, 0}, {1, 0, 2}, {2, 0, 1}, {2, 1, 0}};
	const int *s = sequence[std::clamp(order, 0, 5)];
	return normalize(mul(q[s[2]], mul(q[s[1]], q[s[0]])));
}

// ---- アニメーションカーブ ----

struct Curve
{
	std::vector<double> times; // 秒
	std::vector<double> values;
	double defaultValue = 0.0;
	bool valid = false;

	// 自動接線のエルミート補間(極値では傾き0)
	double evaluate(double t) const
	{
		const size_t n = times.size();
		if(n == 0){ return defaultValue; }
		if(t <= times.front()){ return values.front(); }
		if(t >= times.back()){ return values.back(); }
		const size_t i1 = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), t) - times.begin());
		const size_t i0 = i1 - 1;
		const double h = times[i1] - times[i0];
		const double u = h > 1e-12 ? (t - times[i0]) / h : 0.0;
		const double m0 = slope(i0), m1 = slope(i1);
		const double u2 = u * u, u3 = u2 * u;
		return (2 * u3 - 3 * u2 + 1) * values[i0] + (u3 - 2 * u2 + u) * h * m0 + (-2 * u3 + 3 * u2) * values[i1] + (u3 - u2) * h * m1;
	}

private:
	double slope(size_t i) const
	{
		const size_t n = times.size();
		if(i == 0){ return (values[1] - values[0]) / std::max(times[1] - times[0], 1e-12); }
		if(i + 1 == n){ return (values[n - 1] - values[n - 2]) / std::max(times[n - 1] - times[n - 2], 1e-12); }
		const double a = values[i] - values[i - 1], b = values[i + 1] - values[i];
		if(a * b <= 0.0){ return 0.0; }
		return (values[i + 1] - values[i - 1]) / std::max(times[i + 1] - times[i - 1], 1e-12);
	}
};

struct Model
{
	int64_t id = 0;
	std::string name;
	int64_t parent = 0;
	double translation[3] = {0, 0, 0};
	double rotation[3] = {0, 0, 0};
	double preRotation[3] = {0, 0, 0};
	int rotationOrder = 0;
	Curve translationCurve[3];
	Curve rotationCurve[3];
};

// MixamoのボーンをVRMのヒューマノイド名にする(対応が無ければ空文字列)
std::string humanoidName(std::string name)
{
	const auto colon = name.find(':');
	if(colon != std::string::npos){ name = name.substr(colon + 1); }
	static const std::unordered_map<std::string, std::string> kBody = {
		{"Hips", "hips"}, {"Spine", "spine"}, {"Spine1", "chest"}, {"Spine2", "upperChest"}, {"Neck", "neck"}, {"Head", "head"},
	};
	const auto body = kBody.find(name);
	if(body != kBody.end()){ return body->second; }
	std::string side;
	if(name.rfind("Left", 0) == 0){ side = "left"; name = name.substr(4); }
	else if(name.rfind("Right", 0) == 0){ side = "right"; name = name.substr(5); }
	else{ return std::string(); }
	static const std::unordered_map<std::string, std::string> kLimb = {
		{"Shoulder", "Shoulder"}, {"Arm", "UpperArm"}, {"ForeArm", "LowerArm"}, {"Hand", "Hand"},
		{"UpLeg", "UpperLeg"}, {"Leg", "LowerLeg"}, {"Foot", "Foot"}, {"ToeBase", "Toes"},
		{"HandThumb1", "ThumbMetacarpal"}, {"HandThumb2", "ThumbProximal"}, {"HandThumb3", "ThumbDistal"},
		{"HandIndex1", "IndexProximal"}, {"HandIndex2", "IndexIntermediate"}, {"HandIndex3", "IndexDistal"},
		{"HandMiddle1", "MiddleProximal"}, {"HandMiddle2", "MiddleIntermediate"}, {"HandMiddle3", "MiddleDistal"},
		{"HandRing1", "RingProximal"}, {"HandRing2", "RingIntermediate"}, {"HandRing3", "RingDistal"},
		{"HandPinky1", "LittleProximal"}, {"HandPinky2", "LittleIntermediate"}, {"HandPinky3", "LittleDistal"},
	};
	const auto limb = kLimb.find(name);
	return limb == kLimb.end() ? std::string() : side + limb->second;
}

} // namespace

std::shared_ptr<HumanoidAnimation> loadFbxAnimation(const std::string &fullPath)
{
	size_t fileSize = 0;
	void *fileData = SDL_LoadFile(fullPath.c_str(), &fileSize);
	if(!fileData){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "FBX open error. %s (%s)", SDL_GetError(), fullPath.c_str());
		return nullptr;
	}
	auto result = std::make_shared<HumanoidAnimation>();
	try{
		const uint8_t *data = static_cast<const uint8_t *>(fileData);
		if(fileSize < 27 || std::memcmp(data, "Kaydara FBX Binary  ", 20) != 0){
			throw std::runtime_error("not a binary FBX file");
		}
		uint32_t version;
		std::memcpy(&version, data + 23, 4);
		Reader reader(data, fileSize, version);
		std::vector<FbxNode> top;
		for(size_t pos = 27; pos + 13 < fileSize;){
			FbxNode node;
			if(!reader.readNode(pos, node)){
				break;
			}
			top.push_back(std::move(node));
		}
		auto find = [&](const char *name) -> const FbxNode * {
			for(const auto &n : top){ if(n.name == name){ return &n; } }
			return nullptr;
		};
		const FbxNode *objects = find("Objects");
		const FbxNode *connections = find("Connections");
		if(!objects || !connections){
			throw std::runtime_error("no Objects/Connections");
		}
		// 単位(1 = 1cmなら、メートルへ0.01)
		double unitScale = 0.01;
		if(const FbxNode *settings = find("GlobalSettings")){
			if(const FbxNode *props = settings->find("Properties70")){
				for(const auto &p : props->children){
					if(p.props.size() >= 5 && p.props[0].s == "UnitScaleFactor"){
						unitScale = p.props[4].d * 0.01;
					}
				}
			}
		}

		std::map<int64_t, Model> models;
		std::map<int64_t, std::pair<int64_t, std::string>> curveNodeTarget; // カーブノード → (モデル, プロパティ名)
		std::map<int64_t, Curve> curves;
		for(const auto &o : *&objects->children){
			if(o.props.empty()){ continue; }
			const int64_t id = o.props[0].i;
			if(o.name == "Model"){
				Model m;
				m.id = id;
				m.name = o.props.size() > 1 ? o.props[1].s.substr(0, o.props[1].s.find('\0')) : std::string();
				if(const FbxNode *props = o.find("Properties70")){
					for(const auto &p : props->children){
						if(p.props.size() < 5){ continue; }
						const std::string &key = p.props[0].s;
						double *target = key == "Lcl Translation" ? m.translation : (key == "Lcl Rotation" ? m.rotation : (key == "PreRotation" ? m.preRotation : nullptr));
						if(target && p.props.size() >= 7){
							for(int k = 0; k < 3; ++k){ target[k] = p.props[4 + static_cast<size_t>(k)].d; }
						}
						if(key == "RotationOrder"){ m.rotationOrder = static_cast<int>(p.props[4].i); }
					}
				}
				models[id] = std::move(m);
			}
			else if(o.name == "AnimationCurve"){
				Curve c;
				const FbxNode *keyTime = o.find("KeyTime");
				const FbxNode *keyValue = o.find("KeyValueFloat");
				if(keyTime && keyValue && !keyTime->props.empty() && !keyValue->props.empty()){
					const auto &t = keyTime->props[0].numbers;
					const auto &v = keyValue->props[0].numbers;
					const size_t n = std::min(t.size(), v.size());
					for(size_t k = 0; k < n; ++k){
						c.times.push_back(t[k] / 46186158000.0); // KTime: 1秒 = 46186158000
						c.values.push_back(v[k]);
					}
					c.valid = n > 0;
				}
				if(const FbxNode *def = o.find("Default")){
					if(!def->props.empty()){ c.defaultValue = def->props[0].d; }
				}
				curves[id] = std::move(c);
			}
		}
		// 接続: モデルの親・カーブノードの対象・カーブとカーブノード
		std::map<int64_t, std::vector<std::pair<int64_t, std::string>>> curveOfNode; // カーブノード → (カーブ, "d|X"など)
		for(const auto &c : connections->children){
			if(c.name != "C" || c.props.size() < 3){ continue; }
			const std::string kind = c.props[0].s;
			const int64_t child = c.props[1].i, parent = c.props[2].i;
			if(kind == "OO" && models.count(child) && models.count(parent)){
				models[child].parent = parent;
			}
			else if(kind == "OP" && c.props.size() >= 4){
				const std::string label = c.props[3].s;
				if(models.count(parent) && (label == "Lcl Translation" || label == "Lcl Rotation")){
					curveNodeTarget[child] = {parent, label};
				}
				else if(curves.count(child)){
					curveOfNode[parent].emplace_back(child, label);
				}
			}
		}
		double duration = 0.0;
		for(const auto &entry : curveNodeTarget){
			auto model = models.find(entry.second.first);
			for(const auto &cv : curveOfNode[entry.first]){
				const int axis = cv.second == "d|X" ? 0 : (cv.second == "d|Y" ? 1 : (cv.second == "d|Z" ? 2 : -1));
				if(axis < 0 || model == models.end()){ continue; }
				Curve &curve = curves[cv.first];
				(entry.second.second == "Lcl Translation" ? model->second.translationCurve[axis] : model->second.rotationCurve[axis]) = curve;
				if(curve.valid){ duration = std::max(duration, curve.times.back()); }
			}
		}
		if(duration <= 0.0){
			throw std::runtime_error("no animation curves");
		}

		// 親が先になる順(階層をたどる)
		std::vector<int64_t> order;
		{
			std::map<int64_t, int> state;
			std::vector<int64_t> stack;
			for(const auto &m : models){
				std::vector<int64_t> chain;
				int64_t cur = m.first;
				while(cur != 0 && models.count(cur) && state[cur] == 0 && chain.size() < 1000){
					state[cur] = 1;
					chain.push_back(cur);
					cur = models[cur].parent;
				}
				for(auto it = chain.rbegin(); it != chain.rend(); ++it){ order.push_back(*it); }
			}
		}

		// 人型のボーン
		std::map<int64_t, std::string> humanoid;
		for(const auto &m : models){
			const std::string name = humanoidName(m.second.name);
			if(!name.empty()){ humanoid[m.first] = name; }
		}
		if(humanoid.count(0) == 0 && humanoid.size() < 10){
			throw std::runtime_error("no Mixamo humanoid bones (mixamorig:*)");
		}
		auto humanoidParent = [&](int64_t id) -> int64_t {
			int64_t p = models[id].parent;
			while(p != 0 && models.count(p) && !humanoid.count(p)){ p = models[p].parent; }
			return p != 0 && humanoid.count(p) ? p : 0;
		};

		// 休止ポーズの回転と位置(Lcl Rotation/Translationの既定値、ワールド)
		auto evaluateGlobals = [&](double time, bool restPose, std::map<int64_t, Q> &rotation, std::map<int64_t, std::array<double, 3>> &position){
			for(const int64_t id : order){
				const Model &m = models[id];
				double t[3], r[3];
				for(int k = 0; k < 3; ++k){
					t[k] = restPose || !m.translationCurve[k].valid ? m.translation[k] : m.translationCurve[k].evaluate(time);
					r[k] = restPose || !m.rotationCurve[k].valid ? m.rotation[k] : m.rotationCurve[k].evaluate(time);
				}
				const Q local = mul(eulerToQuat(m.preRotation, 0), eulerToQuat(r, m.rotationOrder));
				const bool hasParent = m.parent != 0 && models.count(m.parent);
				const Q parentRot = hasParent ? rotation[m.parent] : Q{};
				rotation[id] = normalize(mul(parentRot, local));
				const Q offset = rotate(parentRot, t);
				const std::array<double, 3> parentPos = hasParent ? position[m.parent] : std::array<double, 3>{0, 0, 0};
				position[id] = {parentPos[0] + offset.x * unitScale, parentPos[1] + offset.y * unitScale, parentPos[2] + offset.z * unitScale};
			}
		};
		std::map<int64_t, Q> restRotation;
		std::map<int64_t, std::array<double, 3>> restPosition;
		evaluateGlobals(0.0, true, restRotation, restPosition);

		// VRMAの休止ポーズの階層
		std::map<int64_t, size_t> trackOf;
		for(const int64_t id : order){
			if(!humanoid.count(id)){ continue; }
			trackOf[id] = result->rotations.size();
			HumanoidAnimation::RotationTrack track;
			track.bone = humanoid[id];
			result->rotations.push_back(std::move(track));
			HumanoidAnimation::RestNode rest;
			rest.bone = humanoid[id];
			const int64_t p = humanoidParent(id);
			rest.parent = p != 0 ? humanoid[p] : std::string();
			const auto &pos = restPosition[id];
			const std::array<double, 3> pp = p != 0 ? restPosition[p] : std::array<double, 3>{0, 0, 0};
			rest.translation = {static_cast<float>(pos[0] - pp[0]), static_cast<float>(pos[1] - pp[1]), static_cast<float>(pos[2] - pp[2])};
			if(p == 0 && rest.bone == "hips"){
				result->hipsRest = {static_cast<float>(pos[0]), static_cast<float>(pos[1]), static_cast<float>(pos[2])};
			}
			result->rest.push_back(std::move(rest));
		}
		int64_t hipsId = 0;
		for(const auto &h : humanoid){ if(h.second == "hips"){ hipsId = h.first; } }

		constexpr double kFps = 30.0;
		const size_t samples = static_cast<size_t>(std::ceil(duration * kFps)) + 1;
		std::map<int64_t, Q> rotation, delta;
		std::map<int64_t, std::array<double, 3>> position;
		for(size_t s = 0; s < samples; ++s){
			const double time = std::min(static_cast<double>(s) / kFps, duration);
			rotation.clear();
			position.clear();
			evaluateGlobals(time, false, rotation, position);
			for(const auto &h : humanoid){
				delta[h.first] = normalize(mul(rotation[h.first], conj(restRotation[h.first])));
			}
			for(const auto &h : humanoid){
				const int64_t p = humanoidParent(h.first);
				const Q parentDelta = p != 0 ? delta[p] : Q{};
				Q q = normalize(mul(conj(parentDelta), delta[h.first]));
				auto &track = result->rotations[trackOf[h.first]];
				if(!track.values.empty()){
					const Quat &prev = track.values.back();
					if(prev.x * q.x + prev.y * q.y + prev.z * q.z + prev.w * q.w < 0.0){ q = {-q.x, -q.y, -q.z, -q.w}; }
				}
				track.times.push_back(static_cast<float>(time));
				track.values.push_back(Quat{static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)});
			}
			if(hipsId != 0){
				const auto &pos = position[hipsId];
				result->hipsTimes.push_back(static_cast<float>(time));
				result->hipsTranslations.push_back({static_cast<float>(pos[0]), static_cast<float>(pos[1]), static_cast<float>(pos[2])});
			}
		}
		result->duration = static_cast<float>(duration);
		// 歩きなど、腰が水平に進み続けるモーション(ルートモーション)は、その場で足踏みする形にする。
		// 始まりと終わりの水平位置の差を、時間に比例して引く(ループしたとき、元の位置へ戻って跳ばないように)
		if(result->hipsTranslations.size() >= 2){
			auto &t = result->hipsTranslations;
			const float dx = t.back().x - t.front().x, dz = t.back().z - t.front().z;
			if(std::sqrt(dx * dx + dz * dz) > 0.1f){
				for(size_t k = 0; k < t.size(); ++k){
					const float f = static_cast<float>(result->hipsTimes[k] / duration);
					t[k].x -= dx * f;
					t[k].z -= dz * f;
				}
				SDL_Log("FBX: removed root motion (%.2f m, %.2f m) to loop in place", dx, dz);
			}
		}
		SDL_Log("FBX: %zu humanoid bones, %zu samples, %.2f s (unit scale %.4f)", humanoid.size(), samples, duration, unitScale);
	}
	catch(const std::exception &e){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "FBX parse error: %s (%s)", e.what(), fullPath.c_str());
		result = nullptr;
	}
	SDL_free(fileData);
	return result;
}

} // namespace model
