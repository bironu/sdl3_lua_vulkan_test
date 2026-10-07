#include "model/PmxLoader.h"
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <cstring>
#include <stdexcept>

namespace model
{

namespace
{

// 範囲外を読んだらstd::runtime_errorを投げる、リトルエンディアン前提のバイナリ読み取り
class Reader
{
public:
	Reader(const uint8_t *data, size_t size) : data_(data), size_(size), pos_(0) {}

	template<typename T>
	T read()
	{
		T value;
		require(sizeof(T));
		std::memcpy(&value, data_ + pos_, sizeof(T));
		pos_ += sizeof(T);
		return value;
	}
	void skip(size_t n)
	{
		require(n);
		pos_ += n;
	}
	// サイズ(1,2,4バイト)が実行時に決まる整数。signedならそのサイズの符号付き整数として読む
	int32_t readIndex(int size, bool isSigned)
	{
		switch(size){
		case 1: return isSigned ? static_cast<int32_t>(read<int8_t>()) : static_cast<int32_t>(read<uint8_t>());
		case 2: return isSigned ? static_cast<int32_t>(read<int16_t>()) : static_cast<int32_t>(read<uint16_t>());
		case 4: return read<int32_t>();
		default: throw std::runtime_error("bad index size");
		}
	}
	size_t position() const { return pos_; }

private:
	void require(size_t n) const
	{
		if(pos_ + n > size_){
			throw std::runtime_error("unexpected end of file");
		}
	}
	const uint8_t *data_;
	size_t size_;
	size_t pos_;
};

// UTF-16LE → UTF-8(サロゲートペア対応)
std::string utf16ToUtf8(const uint8_t *bytes, size_t byteCount)
{
	std::string out;
	for(size_t i = 0; i + 1 < byteCount; i += 2){
		uint32_t c = static_cast<uint32_t>(bytes[i]) | (static_cast<uint32_t>(bytes[i + 1]) << 8);
		if(c >= 0xD800 && c <= 0xDBFF && i + 3 < byteCount){
			const uint32_t low = static_cast<uint32_t>(bytes[i + 2]) | (static_cast<uint32_t>(bytes[i + 3]) << 8);
			if(low >= 0xDC00 && low <= 0xDFFF){
				c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
				i += 2;
			}
		}
		if(c < 0x80){
			out += static_cast<char>(c);
		}
		else if(c < 0x800){
			out += static_cast<char>(0xC0 | (c >> 6));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		else if(c < 0x10000){
			out += static_cast<char>(0xE0 | (c >> 12));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		else{
			out += static_cast<char>(0xF0 | (c >> 18));
			out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
	}
	return out;
}

} // namespace

std::shared_ptr<ModelData> loadPmx(const std::string &fullPath, const std::string &relativeDir)
{
	size_t fileSize = 0;
	void *fileData = SDL_LoadFile(fullPath.c_str(), &fileSize);
	if(!fileData){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "PMX open error. %s (%s)", SDL_GetError(), fullPath.c_str());
		return nullptr;
	}

	auto result = std::make_shared<ModelData>();
	try{
		Reader r(static_cast<const uint8_t *>(fileData), fileSize);

		// ヘッダー
		char magic[4];
		for(char &c : magic){ c = r.read<char>(); }
		if(std::memcmp(magic, "PMX ", 4) != 0){
			throw std::runtime_error("not a PMX file");
		}
		const float version = r.read<float>();
		const uint8_t globalCount = r.read<uint8_t>();
		if(globalCount < 8){
			throw std::runtime_error("bad PMX header");
		}
		uint8_t globals[8];
		for(uint8_t &g : globals){ g = r.read<uint8_t>(); }
		r.skip(globalCount - 8);
		const bool utf8Text = globals[0] == 1; // 0=UTF-16LE, 1=UTF-8
		const int additionalUv = globals[1];
		const int vertexIndexSize = globals[2];
		const int textureIndexSize = globals[3];
		const int materialIndexSize = globals[4]; // 材質モーフで使う
		const int boneIndexSize = globals[5];
		(void)version;

		// 文字列(長さ付き)。使うのはモデル名とテクスチャのパスだけ
		auto readText = [&]() -> std::string {
			const int32_t length = r.read<int32_t>();
			if(length < 0){
				throw std::runtime_error("bad text length");
			}
			const size_t start = r.position();
			r.skip(static_cast<size_t>(length));
			const uint8_t *p = static_cast<const uint8_t *>(fileData) + start;
			return utf8Text ? std::string(reinterpret_cast<const char *>(p), length) : utf16ToUtf8(p, length);
		};
		result->name = readText(); // モデル名(日本語)
		readText();                // モデル名(英語)
		readText();                // コメント(日本語)
		readText();                // コメント(英語)

		// 頂点
		const int32_t vertexCount = r.read<int32_t>();
		result->vertices.resize(vertexCount);
		for(int32_t i = 0; i < vertexCount; ++i){
			ModelVertex &v = result->vertices[i];
			for(float &f : v.position){ f = r.read<float>(); }
			for(float &f : v.normal){ f = r.read<float>(); }
			for(float &f : v.uv){ f = r.read<float>(); }
			r.skip(sizeof(float) * 4 * additionalUv);
			// ウェイト(ボーンのインデックスは符号付き。-1は無し)
			const uint8_t weightType = r.read<uint8_t>();
			switch(weightType){
			case 0: // BDEF1
				v.bones[0] = r.readIndex(boneIndexSize, true);
				v.weights[0] = 1.0f;
				break;
			case 1: // BDEF2
			case 3: // SDEF(2ボーンのBDEF2として扱う。補正用のC,R0,R1は読み飛ばす)
				v.bones[0] = r.readIndex(boneIndexSize, true);
				v.bones[1] = r.readIndex(boneIndexSize, true);
				v.weights[0] = r.read<float>();
				v.weights[1] = 1.0f - v.weights[0];
				if(weightType == 3){
					r.skip(sizeof(float) * 9);
				}
				break;
			case 2: // BDEF4
			case 4: // QDEF(2.1。BDEF4として扱う)
				for(int &b : v.bones){ b = r.readIndex(boneIndexSize, true); }
				for(float &w : v.weights){ w = r.read<float>(); }
				break;
			default:
				throw std::runtime_error("bad weight type");
			}
			r.skip(sizeof(float)); // エッジ倍率
		}

		// 面(頂点番号。1,2バイトは符号なし)
		const int32_t indexCount = r.read<int32_t>();
		if(indexCount < 0 || indexCount % 3 != 0){
			throw std::runtime_error("bad index count");
		}
		result->indices.resize(indexCount);
		for(int32_t i = 0; i < indexCount; ++i){
			const int32_t index = r.readIndex(vertexIndexSize, vertexIndexSize == 4);
			if(index < 0 || index >= vertexCount){
				throw std::runtime_error("vertex index out of range");
			}
			result->indices[i] = static_cast<uint32_t>(index);
		}

		// テクスチャ(PMXのファイルからの相対パス。区切りは\\のことがある)
		const int32_t textureCount = r.read<int32_t>();
		for(int32_t i = 0; i < textureCount; ++i){
			std::string path = readText();
			for(char &c : path){
				if(c == '\\'){ c = '/'; }
			}
			result->texturePaths.push_back(relativeDir.empty() ? path : relativeDir + "/" + path);
		}

		// 材質
		const int32_t materialCount = r.read<int32_t>();
		uint32_t firstIndex = 0;
		for(int32_t i = 0; i < materialCount; ++i){
			ModelMaterial m;
			m.name = readText();
			readText(); // 英語名
			for(float &f : m.diffuse){ f = r.read<float>(); }
			float specularColor[3];
			for(float &f : specularColor){ f = r.read<float>(); }
			const float specularPower = r.read<float>();
			r.skip(sizeof(float) * 3); // 環境色
			const uint8_t flags = r.read<uint8_t>();
			r.skip(sizeof(float) * 4 + sizeof(float)); // エッジ色、エッジサイズ
			const int32_t texture = r.readIndex(textureIndexSize, true);
			r.readIndex(textureIndexSize, true); // スフィアテクスチャ
			r.skip(1);                           // スフィアモード
			const uint8_t sharedToon = r.read<uint8_t>();
			if(sharedToon == 0){
				r.readIndex(textureIndexSize, true); // トゥーンテクスチャ
			}
			else{
				r.skip(1); // 共有トゥーン番号
			}
			readText(); // メモ
			const int32_t surfaceCount = r.read<int32_t>();

			m.doubleSided = (flags & 0x01) != 0;
			m.texture = (texture >= 0 && texture < textureCount) ? texture : -1;
			m.specular = (specularColor[0] + specularColor[1] + specularColor[2]) / 3.0f;
			m.shininess = specularPower > 1.0f ? specularPower : 1.0f;
			m.firstIndex = firstIndex;
			m.indexCount = static_cast<uint32_t>(surfaceCount);
			firstIndex += m.indexCount;
			result->materials.push_back(std::move(m));
		}
		if(firstIndex > static_cast<uint32_t>(indexCount)){
			throw std::runtime_error("material index range out of bounds");
		}

		// ボーン
		const int32_t boneCount = r.read<int32_t>();
		result->bones.resize(boneCount);
		for(int32_t i = 0; i < boneCount; ++i){
			ModelBone &b = result->bones[i];
			b.name = readText();
			readText(); // 英語名
			for(float &f : b.position){ f = r.read<float>(); }
			b.parent = r.readIndex(boneIndexSize, true);
			b.layer = r.read<int32_t>();
			b.flags = r.read<uint16_t>();
			if(b.flags & ModelBone::TailIsBone){
				r.readIndex(boneIndexSize, true); // 先端のボーン(使わない)
			}
			else{
				r.skip(sizeof(float) * 3); // 先端のオフセット(使わない)
			}
			if(b.flags & (ModelBone::AppendRotation | ModelBone::AppendTranslation)){
				b.appendParent = r.readIndex(boneIndexSize, true);
				b.appendRatio = r.read<float>();
			}
			if(b.flags & ModelBone::FixedAxis){
				r.skip(sizeof(float) * 3);
			}
			if(b.flags & ModelBone::LocalAxis){
				r.skip(sizeof(float) * 6);
			}
			if(b.flags & ModelBone::ExternalParent){
				r.skip(sizeof(int32_t));
			}
			if(b.flags & ModelBone::IK){
				b.ikTarget = r.readIndex(boneIndexSize, true);
				b.ikLoop = r.read<int32_t>();
				b.ikLimit = r.read<float>();
				const int32_t linkCount = r.read<int32_t>();
				if(linkCount < 0 || linkCount > 1000){
					throw std::runtime_error("bad IK link count");
				}
				b.ikLinks.resize(linkCount);
				for(auto &link : b.ikLinks){
					link.bone = r.readIndex(boneIndexSize, true);
					link.hasLimit = r.read<uint8_t>() != 0;
					if(link.hasLimit){
						for(float &f : link.limitMin){ f = r.read<float>(); }
						for(float &f : link.limitMax){ f = r.read<float>(); }
					}
				}
			}
		}

		// モーフ。ここで失敗しても(未知の種類など)、モデル自体は使えるので、読めたところまでで止める
		try{
			const int morphIndexSize = globals[6];
			const int rigidIndexSize = globals[7];
			const int32_t morphCount = r.read<int32_t>();
			if(morphCount < 0 || morphCount > 100000){
				throw std::runtime_error("bad morph count");
			}
			for(int32_t i = 0; i < morphCount; ++i){
				ModelMorph morph;
				morph.name = readText();
				readText(); // 英語名
				r.skip(1);  // 操作パネル
				const uint8_t type = r.read<uint8_t>();
				const int32_t offsetCount = r.read<int32_t>();
				if(offsetCount < 0 || type > 10){
					throw std::runtime_error("bad morph");
				}
				morph.type = static_cast<ModelMorph::Type>(type);
				for(int32_t k = 0; k < offsetCount; ++k){
					switch(morph.type){
					case ModelMorph::Type::Group:
					case ModelMorph::Type::Flip:{
						ModelMorph::GroupOffset g;
						g.morph = r.readIndex(morphIndexSize, true);
						g.rate = r.read<float>();
						morph.groupOffsets.push_back(g);
						break;
					}
					case ModelMorph::Type::Vertex:{
						ModelMorph::VertexOffset v;
						v.vertex = static_cast<uint32_t>(r.readIndex(vertexIndexSize, vertexIndexSize == 4));
						for(float &f : v.delta){ f = r.read<float>(); }
						morph.vertexOffsets.push_back(v);
						break;
					}
					case ModelMorph::Type::Bone:
						r.readIndex(boneIndexSize, true);
						r.skip(sizeof(float) * 7); // 移動3 + 回転4
						break;
					case ModelMorph::Type::Material:{
						ModelMorph::MaterialOffset m;
						m.material = r.readIndex(materialIndexSize, true);
						m.add = r.read<uint8_t>() != 0;
						for(float &f : m.diffuse){ f = r.read<float>(); }
						r.skip(sizeof(float) * 24); // 鏡面・鏡面係数・環境光・輪郭・テクスチャ/スフィア/トゥーン係数
						morph.materialOffsets.push_back(m);
						break;
					}
					case ModelMorph::Type::Uv:{
						ModelMorph::UvOffset u;
						u.vertex = static_cast<uint32_t>(r.readIndex(vertexIndexSize, vertexIndexSize == 4));
						for(float &f : u.delta){ f = r.read<float>(); }
						morph.uvOffsets.push_back(u);
						break;
					}
					case ModelMorph::Type::Impulse:
						r.readIndex(rigidIndexSize, true);
						r.skip(1 + sizeof(float) * 6);
						break;
					default: // 追加UV
						r.readIndex(vertexIndexSize, vertexIndexSize == 4);
						r.skip(sizeof(float) * 4);
						break;
					}
				}
				result->morphs.push_back(std::move(morph));
			}
		}
		catch(const std::exception &e){
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "PMX morph parse stopped: %s (%zu morphs read)", e.what(), result->morphs.size());
		}

		// 表示枠(読み飛ばす)・剛体・ジョイント。物理演算の情報が読めなくても、モデル自体は使える
		try{
			const int morphIndexSize = globals[6];
			const int rigidIndexSize = globals[7];
			const int32_t frameCount = r.read<int32_t>();
			if(frameCount < 0 || frameCount > 100000){
				throw std::runtime_error("bad display frame count");
			}
			for(int32_t i = 0; i < frameCount; ++i){
				readText();
				readText();
				r.skip(1); // 特殊枠フラグ
				const int32_t elementCount = r.read<int32_t>();
				if(elementCount < 0){
					throw std::runtime_error("bad display frame element count");
				}
				for(int32_t k = 0; k < elementCount; ++k){
					const uint8_t type = r.read<uint8_t>();
					r.readIndex(type == 0 ? boneIndexSize : morphIndexSize, true);
				}
			}

			const int32_t rigidCount = r.read<int32_t>();
			if(rigidCount < 0 || rigidCount > 100000){
				throw std::runtime_error("bad rigid body count");
			}
			for(int32_t i = 0; i < rigidCount; ++i){
				ModelRigidBody body;
				body.name = readText();
				readText();
				body.bone = r.readIndex(boneIndexSize, true);
				body.group = r.read<uint8_t>();
				body.nonCollisionMask = r.read<uint16_t>();
				const uint8_t shape = r.read<uint8_t>();
				if(shape > 2){
					throw std::runtime_error("bad rigid body shape");
				}
				body.shape = static_cast<ModelRigidBody::Shape>(shape);
				for(float &f : body.size){ f = r.read<float>(); }
				for(float &f : body.position){ f = r.read<float>(); }
				for(float &f : body.rotation){ f = r.read<float>(); }
				body.mass = r.read<float>();
				body.linearDamping = r.read<float>();
				body.angularDamping = r.read<float>();
				body.restitution = r.read<float>();
				body.friction = r.read<float>();
				const uint8_t type = r.read<uint8_t>();
				if(type > 2){
					throw std::runtime_error("bad rigid body type");
				}
				body.type = static_cast<ModelRigidBody::Type>(type);
				result->rigidBodies.push_back(std::move(body));
			}

			const int32_t jointCount = r.read<int32_t>();
			if(jointCount < 0 || jointCount > 100000){
				throw std::runtime_error("bad joint count");
			}
			for(int32_t i = 0; i < jointCount; ++i){
				ModelJoint joint;
				joint.name = readText();
				readText();
				r.read<uint8_t>(); // 種類(PMX 2.1で増えたが、データの並びはどれも同じ)
				joint.rigidA = r.readIndex(rigidIndexSize, true);
				joint.rigidB = r.readIndex(rigidIndexSize, true);
				for(float *array : {joint.position, joint.rotation, joint.moveLimitMin, joint.moveLimitMax,
					joint.rotationLimitMin, joint.rotationLimitMax, joint.springMove, joint.springRotation}){
					for(int k = 0; k < 3; ++k){ array[k] = r.read<float>(); }
				}
				result->joints.push_back(std::move(joint));
			}
		}
		catch(const std::exception &e){
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "PMX physics parse stopped: %s (%zu rigid bodies, %zu joints read)", e.what(),
				result->rigidBodies.size(), result->joints.size());
		}
	}
	catch(const std::exception &e){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "PMX parse error: %s (%s)", e.what(), fullPath.c_str());
		result = nullptr;
	}
	SDL_free(fileData);
	return result;
}

} // namespace model
