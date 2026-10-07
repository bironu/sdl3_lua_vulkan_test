#if !defined(SDLVULKANWINDOW_H_)
#define SDLVULKANWINDOW_H_

#include "sdl/SDLWindow.h"
#include "geo/Calculator.h"
#include "geo/Matrix.h"
#include "geo/Vector3.h"
#include "vk/Vertex.h"
#include "vk/VulkanBuffer.h"
#include "vk/VulkanContext.h"
#include "vk/VulkanMaterial.h"
#include "vk/VulkanMesh.h"
#include "vk/VulkanBonePool.h"
#include "vk/VulkanModel.h"
#include "vk/VulkanTexture.h"
#include "vk/VulkanTexturePool.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace SDL_
{

class Image;

// Vulkanで描画するウィンドウ(サーフェス+スワップチェーン)。
// swap()で1フレーム(取得→クリア→描画→present)を実行する。
// draw()/drawSprite*()で予約されたものを、予約順に描画する:
//   - draw(): ライティング付きメッシュ(res/shaders/triangle.*。深度書込みあり)。平行光源のシャドウマップ
//     (setShadowArea)と点光源のキューブマップ(setPointShadowRange)に影を落とし/受ける(材質のcastShadow/receiveShadow)
//   - drawSprite2D(): 画面座標のスプライト(res/shaders/sprite.*。アルファブレンド、深度テスト無し)
//   - drawSprite3D(): 3D空間のスプライト(アルファブレンド、深度テストあり・書込み無し。奥から順に予約すること)
// シェーダーは実行ファイルと同じ場所のshaders/*.spv(res/shadersからビルド時に生成)から読み込む。
class VulkanWindow : public Window
{
public:
	// ctxはinitInstance()済みであること。最初のVulkanWindowでデバイスも初期化される
	VulkanWindow(std::shared_ptr<VulkanContext> ctx, const char* title, int x, int y, int w, int h, Uint32 flags);
	~VulkanWindow() override;

	// 初期化(サーフェス・デバイス・スワップチェーン等)に成功したか
	bool isReady() const { return ready_; }
	void setClearColor(float r, float g, float b, float a = 1.0f)
	{
		clearColor_ = {{r, g, b, a}};
	}

	// メッシュを変換行列mvpで描画する予約をする(次のswap()で描画され、予約は消える)。
	// mvp = vk_::createPerspective()などの射影 * ビュー * モデル(vk/VulkanMath.h参照)。
	// 複数回呼べば複数オブジェクトを描ける。meshはGPU使用が終わるまでウィンドウが保持する
	// modelはワールド変換行列(法線の変換に使う。回転+一様スケールのみ対応)
	// materialを省略すると、ウィンドウの既定(setTexture()のテクスチャ + setMaterial()の鏡面設定)で描く
	void draw(std::shared_ptr<VulkanMesh> mesh, const geo::Matrix4x4f& mvp, const geo::Matrix4x4f& model);
	void draw(std::shared_ptr<VulkanMesh> mesh, const geo::Matrix4x4f& mvp, const geo::Matrix4x4f& model,
		const VulkanMaterial& material);
	// モデルの全材質を、モデルファイルの並び順で描く(アルファブレンドのため順番が大事)。
	// viewProjは カメラのビュー射影(クリップ補正込み)、modelMatrixはモデルのワールド変換
	// ボーンのあるモデルは、GPUでスキニングされる(VulkanModel::updatePose()で姿勢を解いておくこと。ボーン行列はswap()で送られる)
	void draw(const std::shared_ptr<VulkanModel>& model, const geo::Matrix4x4f& viewProj, const geo::Matrix4x4f& modelMatrix);
	const std::shared_ptr<VulkanContext>& getContext() const { return ctx_; }
	// ボーン行列のdescriptor setのプール(VulkanModel::createに渡す)
	const std::shared_ptr<VulkanBonePool>& getBonePool() const { return bonePool_; }
	// テクスチャのdescriptor setのプール(VulkanModel::createに渡す。MToonの複数テクスチャの組を作るのに使う)
	const std::shared_ptr<VulkanTexturePool>& getTexturePool() const { return texturePool_; }
	// draw()で使うテクスチャを設定する(全オブジェクト共通)。画像はRGBA8へ変換してコピーされるので、
	// 呼出し後にimageは手放してよい。未設定の間は1x1の白(=頂点色のまま)。内部でvkDeviceWaitIdleする。
	// 画像ファイルの読み込みはウィンドウの責務ではない: Resources::loadImage()/getImage()で得たImageを渡す
	bool setTexture(const std::shared_ptr<SDL_::Image> &image);

	// スプライト用のテクスチャを作る。画像はRGBA8へ変換してコピーされる(呼出し後にimageは手放してよい)。
	// 戻り値はこのウィンドウで描画できる。GPU使用中は内部で保持され、安全に手放せる。失敗時はnullptr
	std::shared_ptr<VulkanTexture> createTexture(const std::shared_ptr<SDL_::Image> &image);
	// createTextureと同じだが、keyごとに1つだけ作って共有する(弱参照のキャッシュ。使う人が全員手放すと、テクスチャは解放される)。
	// 同じ画像を複数のシーンが使うときの、GPUへの重複した転送を避ける。keyにはResourcesの画像のパスなどを使う。メインスレッドから呼ぶ
	std::shared_ptr<VulkanTexture> createCachedTexture(const std::string &key, const std::shared_ptr<SDL_::Image> &image);

	// 2Dスプライトの座標系(論理画面サイズ)。左上原点・y下向き。ウィンドウのリサイズに依存しない
	// シーン(3D)の描画解像度もこのサイズ(内部でこの解像度のバッファへ描いて、後処理でウィンドウへ拡大縮小して出す。縮小時は双線形で平均される)
	void setScreenSize(float width, float height)
	{
		swapchainDirty_ = swapchainDirty_ || width != screenWidth_ || height != screenHeight_;
		screenWidth_ = width;
		screenHeight_ = height;
	}
	float getScreenWidth() const { return screenWidth_; }
	float getScreenHeight() const { return screenHeight_; }
	// 論理画面座標の矩形(左上x,y・幅w・高さh)にテクスチャ全体を描く。r,g,b,aは色とアルファの乗数
	void drawSprite2D(std::shared_ptr<VulkanTexture> texture, float x, float y, float w, float h,
		float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
	// 同、テクスチャの一部(uv: u0, v0, u1, v1。0〜1)だけを貼る。1枚の画像に並べた文字(ビットマップフォント)を、1文字ずつ描くのに使う
	void drawSprite2DRegion(std::shared_ptr<VulkanTexture> texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
		float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
	// 同、中心(cx, cy)のまわりに、radians(時計回りが正。画面はy下向き)だけ回して描く。w,hは回す前の大きさ
	void drawSprite2DRotated(std::shared_ptr<VulkanTexture> texture, float cx, float cy, float w, float h, float radians,
		float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
	// 3D空間のスプライト。mvpは中心原点の1x1の板(x,y: -0.5〜0.5, z=0、+Z向き)に掛ける行列
	// (= vk_::createPerspective()などの射影 * ビュー * モデル)
	void drawSprite3D(std::shared_ptr<VulkanTexture> texture, const geo::Matrix4x4f& mvp,
		float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
	// 平行光源(ワールド空間)。directionは光が進む向き(内部で正規化)。
	// color=拡散光の色、ambient=光が当たらない面の最低限の明るさ
	void setLight(const geo::Vector3f& direction, float r, float g, float b, float ambient)
	{
		const auto d = geo::Vector3f::normalize(direction);
		uniform_.lightDir = {d.getX(), d.getY(), d.getZ(), 0.0f};
		uniform_.lightColor = {r, g, b, 1.0f};
		uniform_.ambient = {ambient, ambient, ambient, 1.0f};
	}
	// 点光源は最大kMaxPointLights個(番号0〜)。距離dでの明るさは 1 / (constant + linear*d + quadratic*d^2)。
	// 色を黒(0,0,0)にすると、その光源は無効。影はキューブマップ(全方向のシャドウマップ)で、光源ごとに6方向へ描く。
	// 影の対象は平行光源と同じ(材質のcastShadow/receiveShadow)。castShadowをfalseにすると、その光源だけ影を落とさない。
	// 届く距離はsetPointShadowRangeで決める(全光源共通)
	static constexpr int kMaxPointLights = 4;
	// 同時に進行するフレーム(フレーム枠)の数。VulkanModel::createに渡す(ボーン行列のバッファを枠ごとに持つため)
	static constexpr int kFrameSlots = 2;
	void setPointLight(int index, const geo::Vector3f& position, float r, float g, float b,
		float constant = 1.0f, float linear = 0.35f, float quadratic = 0.44f, bool castShadow = true)
	{
		if(index < 0 || index >= kMaxPointLights){
			return;
		}
		uniform_.pointPos[index] = {position.getX(), position.getY(), position.getZ(), 1.0f};
		uniform_.pointColor[index] = {r, g, b, 1.0f};
		uniform_.pointAtten[index] = {constant, linear, quadratic, castShadow ? 1.0f : 0.0f};
	}
	// 番号0の点光源(1つだけ使うとき用)
	void setPointLight(const geo::Vector3f& position, float r, float g, float b,
		float constant = 1.0f, float linear = 0.35f, float quadratic = 0.44f)
	{
		setPointLight(0, position, r, g, b, constant, linear, quadratic);
	}
	// 全部の点光源を無効にする
	void clearPointLights()
	{
		for(auto& color : uniform_.pointColor){
			color = {0.0f, 0.0f, 0.0f, 1.0f};
		}
	}
	// カメラ位置(ワールド空間)。鏡面反射の視線方向の計算に使う
	void setCameraPosition(const geo::Vector3f& eye)
	{
		uniform_.cameraPos = {eye.getX(), eye.getY(), eye.getZ(), 1.0f};
	}
	// シャドウマップの範囲(ワールド空間の球: 中心とその半径)。平行光源は常にこの球の中心を向き、
	// 範囲内の物体の影が落ちる。大きいほど広く影が付くが、影の解像度(2048x2048をこの範囲に割り当てる)は粗くなる。
	// 影を落とす/受けるのは平行光源の光だけ(点光源・環境光には影が付かない)
	void setShadowArea(const geo::Vector3f& center, float radius)
	{
		shadowCenter_ = center;
		shadowRadius_ = radius;
	}
	// 影の品質。ぼかし半径はシャドウマップのテクセル単位(大きいほど影の縁がやわらかい。0でぼかし無し)、
	// 法線オフセットは参照位置を面の法線方向へずらす量(テクセル単位。大きいほど斜めの面の縞は消えるが、影が物体から離れる)
	void setShadowQuality(float pcfRadiusTexels, float normalOffsetTexels, float pointPcfRadiusTexels)
	{
		uniform_.shadowParams[0] = pcfRadiusTexels;
		uniform_.shadowParams[1] = normalOffsetTexels;
		uniform_.shadowParams[2] = pointPcfRadiusTexels;
	}
	// 環境光の改善(トゥーン以外)。IBLの代わりの半球の環境光(上=setLightのambient、下=groundの色: 地面・壁からの照り返し)、
	// 接地AO(床に近く下向きの面を暗く)、ハーフランバート風のまわり込み(皮膚の陰が硬く黒くならないよう、赤みを帯びて回り込む)。
	// 全部まとめて有効にする。enable=falseで従来の一様な環境光
	void setAmbientEnvironment(bool enable, const geo::Vector3f& ground = {0.3f, 0.28f, 0.25f}, float floorHeight = 0.0f,
		float contactAo = 0.5f, float wrapLighting = 0.4f)
	{
		uniform_.ambientGround = {ground.getX(), ground.getY(), ground.getZ(), 1.0f};
		uniform_.envParams = {floorHeight, wrapLighting, contactAo, enable ? 1.0f : 0.0f};
	}
	// トゥーンシェーディング(陰影を3段に分け、鏡面を「ある/なし」にする)。ライティング付きのメッシュすべてに掛かる
	void setToonShading(bool enable) { uniform_.pointShadow[3] = enable ? 1.0f : 0.0f; }
	bool isToonShading() const { return uniform_.pointShadow[3] > 0.5f; }
	// トゥーンシェーディング時の輪郭線の太さ(ピクセル。0で輪郭線なし)。反転した殻を描く方式で、メッシュの裏面を法線方向へふくらませて黒く塗る
	void setOutlineWidth(float pixels) { outlineWidth_ = pixels; }
	float outlineWidth() const { return outlineWidth_; }
	// 点光源の影が届く最大距離(ワールド単位)。光源からこの距離までの物体が影を落とす/受ける。
	// 大きいほど遠くまで届くが、深度の精度は粗くなる
	void setPointShadowRange(float farDistance)
	{
		uniform_.pointShadow[0] = farDistance;
	}
	// 点光源の影の絞り込み: 光の強さ(最大の色成分 x 距離の減衰)がこの値を下回る距離より遠い物は、その光源の影のパスに描かない。
	// 大きいほど絞る(既定0.05=光が5%未満になる距離から先は影を落とさない)。0で絞り込まない
	void setPointShadowCutoff(float intensity) { pointShadowCutoff_ = intensity; }
	float pointShadowCutoff() const { return pointShadowCutoff_; }
	// MToonの輪郭線(モデルの単位の太さ)が、画面上でこのピクセル数より細くなったら描かない(遠くのモデルの負荷を省く。既定0.3、0で無効)
	void setOutlineMinPixels(float pixels) { outlineMinPixels_ = pixels; }
	// シャドウマップ(平行光・点光源の影)の有効/無効(既定は有効)。無効にすると、影のパスを描かず(最初の1回のクリアだけ)、
	// シェーダーも影を参照しないので、その分の負荷が無くなる。地面の影などは、呼び出し側が丸い影(ブロブ)などで描く
	void setShadowMapsEnabled(bool enable) { shadowMapsEnabled_ = enable; }
	bool shadowMapsEnabled() const { return shadowMapsEnabled_; }
	// 点光源の影の更新間隔(フレーム枠の使用回数。1=毎回、2=2回に1回…)。光源ごとにずらして描き直し、描かない回は前回のシャドウマップを使う。
	// 大きいほど軽いが、動く物の影が遅れる(最大で 間隔*枠数 フレーム)
	void setPointShadowInterval(int frames) { pointShadowInterval_ = std::max(1, frames); }
	int pointShadowInterval() const { return pointShadowInterval_; }
	// 既定の材質の鏡面設定(materialを指定しないdraw()に使われる)。
	// specularは鏡面反射の強さ(0で無し)、shininessは光沢度(大きいほどハイライトが小さく鋭い)
	void setMaterial(float specular, float shininess)
	{
		defaultSpecular_ = specular;
		defaultShininess_ = shininess;
	}
	// フラグメントシェーダーで色に掛ける係数(uniform buffer)
	void setTint(float r, float g, float b, float a = 1.0f) { uniform_.tint = {r, g, b, a}; }

	// アンチエイリアス。シーンをオフスクリーンへ描き、最後に全画面の後処理でスワップチェーンへ出す。
	// Msaa4x=描画時の4xマルチサンプル(エッジ・シェーダーの外側)、Fxaa=後処理でのエッジぼかし(画面全体)、両方=Msaa4xFxaa。
	// 4xに対応しないGPUではMsaa4xはNoneと同じ。2Dスプライト(UI)も後処理の対象になる
	enum class AntiAliasing { None, Msaa4x, Fxaa, Msaa4xFxaa };
	void setAntiAliasing(AntiAliasing mode) { antiAliasing_ = mode; }
	AntiAliasing antiAliasing() const { return antiAliasing_; }
	// None → Msaa4x → Fxaa → Msaa4xFxaa → None の順に切り替える
	AntiAliasing cycleAntiAliasing()
	{
		antiAliasing_ = static_cast<AntiAliasing>((static_cast<int>(antiAliasing_) + 1) % 4);
		return antiAliasing_;
	}
	static const char* antiAliasingName(AntiAliasing mode)
	{
		static const char* const names[] = {"None", "MSAA 4x", "FXAA", "MSAA 4x + FXAA"};
		return names[static_cast<int>(mode)];
	}
	// フラスタムカリング: 視錐台(影のパスでは光の視錐台)の外にあるメッシュを描かない(既定は有効)。
	// 判定はメッシュ(スキニングのモデルは、現在の姿勢のモデル全体)のAABBで行う
	void setFrustumCulling(bool enable) { frustumCulling_ = enable; }
	bool frustumCulling() const { return frustumCulling_; }
	// 直近のswap()で描いたライティング付きの描画予約の数と、そのうちカリングで省いた数(メインのパス)
	int lastDrawCount() const { return lastDrawCount_; }
	int lastCulledCount() const { return lastCulledCount_; }

	void swap() override;
	void onPixelSizeChanged() override { swapchainDirty_ = true; }

private:
	bool createSwapchain();
	void destroySwapchain();
	bool createFrames();
	bool createDescriptors();
	bool chooseDepthFormat();
	bool createShadowResources();
	bool createShadowPipeline(const char* vertName, VkDescriptorSetLayout setLayout, VkPipelineLayout* layoutOut, VkPipeline* out, bool skinned);
	bool createDepthResources();
	bool createRenderPass();
	bool createPostResources();
	bool createSceneTargets();
	void destroySceneTargets();
	bool createPipelines();
	bool createSkinPipeline();
	bool createGraphicsPipeline(const char* vertName, const char* fragName, VkPipelineLayout layout,
		bool depthTest, bool depthWrite, bool blend, VkPipeline* out, bool skinned = false,
		VkCullModeFlags cullMode = VK_CULL_MODE_NONE, bool msaa = false);
	VkShaderModule loadShader(const char* name);

	std::shared_ptr<VulkanContext> ctx_;
	VkSurfaceKHR surface_ = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
	VkFormat format_ = VK_FORMAT_UNDEFINED;
	VkExtent2D extent_{};       // スワップチェーン(ウィンドウのピクセルサイズ)
	VkExtent2D renderExtent_{}; // シーンの描画解像度(論理画面のサイズ)
	std::vector<VkImage> images_;
	std::vector<VkImageView> imageViews_;
	std::vector<VkFramebuffer> framebuffers_; // 後処理のパス用(スワップチェーンのイメージごと。カラー1枚)
	// 深度バッファ(swapchainと同サイズ。リサイズ時に作り直す)
	VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
	VkImage depthImage_ = VK_NULL_HANDLE;
	VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
	VkImageView depthView_ = VK_NULL_HANDLE;
	// シーンはオフスクリーン(sceneImage_)へ描き、postRenderPass_でスワップチェーンへ出す(FXAAはこのパスで掛ける)。
	// renderPass_=1x、renderPassMsaa_=4x(sceneImage_へリゾルブ)。どちらもフォーマット確定後に一度だけ作る
	struct Attachment
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
	};
	bool createAttachment(VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageAspectFlags aspect, Attachment& out);
	void destroyAttachment(Attachment& a);
	VkRenderPass renderPass_ = VK_NULL_HANDLE;
	VkRenderPass renderPassMsaa_ = VK_NULL_HANDLE;
	VkRenderPass postRenderPass_ = VK_NULL_HANDLE;
	bool msaaSupported_ = false;
	Attachment sceneColor_;     // 1x。シーンの結果(後処理が参照する)
	Attachment msaaColor_;      // 4x
	Attachment msaaDepth_;      // 4x
	VkFramebuffer sceneFramebuffer_ = VK_NULL_HANDLE;     // sceneColor_ + depth(1x)
	VkFramebuffer sceneMsaaFramebuffer_ = VK_NULL_HANDLE; // msaaColor_ + msaaDepth_ + sceneColor_(リゾルブ)
	VkDescriptorSetLayout postSetLayout_ = VK_NULL_HANDLE;
	VkDescriptorPool postPool_ = VK_NULL_HANDLE;
	VkDescriptorSet postSet_ = VK_NULL_HANDLE;
	VkSampler postSampler_ = VK_NULL_HANDLE;
	VkPipelineLayout postLayout_ = VK_NULL_HANDLE;
	VkPipeline postPipeline_ = VK_NULL_HANDLE;
	AntiAliasing antiAliasing_ = AntiAliasing::None;
	bool frustumCulling_ = true;
	float pointShadowCutoff_ = 0.05f;
	int pointShadowInterval_ = 1;
	bool shadowMapsEnabled_ = true;
	float outlineMinPixels_ = 0.3f; // MToonの輪郭線が画面上でこれより細ければ描かない(0で無効)
	int lastDrawCount_ = 0;
	int lastCulledCount_ = 0;
	// パイプライン3種。ライティング付きは(set0=フレームUBO, set1=テクスチャ)+push 128B(mvp, モデル行列の上3行, 材質)、
	// スプライトは同じdescriptor set配置+push 80B(mvp,color)。スプライト2D/3Dは深度ステートだけが違う
	VkPipelineLayout litLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout spriteLayout_ = VK_NULL_HANDLE;
	// シーンのパイプラインは、1x用と4x用(レンダーパスのサンプル数が違うと使えない)を両方持つ
	struct ScenePipelines
	{
		VkPipeline lit = VK_NULL_HANDLE;
		VkPipeline litSkin = VK_NULL_HANDLE;
		VkPipeline litOpaque = VK_NULL_HANDLE;     // 不透明の材質用(ブレンド・discardなし。TBDRで隠れた面のシェーディングを省ける)
		VkPipeline litSkinOpaque = VK_NULL_HANDLE;
		VkPipeline litMask = VK_NULL_HANDLE;       // アルファの切り抜き(MASK)の材質用(ブレンドなし。しきい値未満をdiscard)
		VkPipeline litSkinMask = VK_NULL_HANDLE;
		VkPipeline outline = VK_NULL_HANDLE;
		VkPipeline outlineSkin = VK_NULL_HANDLE;
		VkPipeline outlineOpaque = VK_NULL_HANDLE;     // アルファの切り抜きが要らない(不透明なテクスチャの)材質用。フラグメントシェーダーがdiscardしない
		VkPipeline outlineSkinOpaque = VK_NULL_HANDLE;
		VkPipeline sprite2d = VK_NULL_HANDLE;
		VkPipeline sprite3d = VK_NULL_HANDLE;
	};
	std::array<ScenePipelines, 2> scenePipelines_; // [0]=1x、[1]=4x
	// スキニング付き(ボーンモデル用): set 2=ボーン行列。頂点はvk_::SkinnedVertex
	VkPipelineLayout litSkinLayout_ = VK_NULL_HANDLE;
	// スキニング(コンピュート): set 0=ボーン行列・モーフ(VulkanBonePool)、set 1=部品ごとの入力/出力の頂点。push=頂点数(uint)
	VkPipelineLayout skinLayout_ = VK_NULL_HANDLE;
	VkPipeline skinPipeline_ = VK_NULL_HANDLE;
	// 輪郭線(反転した殻。表面をカリングして裏面だけを描く): push 80B(mvp, 1ピクセルの大きさと太さ)。スキニング版はset 0=ボーン行列
	VkPipelineLayout outlineLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout outlineSkinLayout_ = VK_NULL_HANDLE;
	float outlineWidth_ = 3.0f;
	// シャドウマップ: 平行光源から見たデプスだけを描くパス。解像度は固定で、フレーム枠ごとに別の画像を持つ
	// (枠をまたいだ書込みと参照が競合しないようにするため)
	static constexpr uint32_t kShadowMapSize = 2048;
	static constexpr uint32_t kPointShadowSize = 512; // 点光源のキューブマップの1面の解像度(光源の数だけ6面ずつ、1枚の配列画像に持つ)
	VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
	VkPipelineLayout shadowLayout_ = VK_NULL_HANDLE;
	VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
	VkPipelineLayout shadowSkinLayout_ = VK_NULL_HANDLE; // set 0=ボーン行列
	VkPipeline shadowSkinPipeline_ = VK_NULL_HANDLE;
	VkSampler shadowSampler_ = VK_NULL_HANDLE;
	geo::Vector3f shadowCenter_{0.0f, 0.0f, 0.0f};
	float shadowRadius_ = 8.0f;
	enum class DrawKind { Lit, LitSkinned, Sprite2D, Sprite3D };
	struct DrawCommand
	{
		DrawKind kind;
		std::shared_ptr<VulkanMesh> mesh;
		std::shared_ptr<VulkanTexture> texture;
		geo::Matrix4x4f mvp;
		geo::Matrix4x4f model;       // Litのみ
		std::array<float, 4> params; // Lit: (鏡面反射の強さ, 光沢度, フラグ(影を受けるか+自発光する点光源の番号), 不透明度)、スプライト: (rgb乗数, アルファ)
		bool castShadow = false;     // Litのみ: シャドウマップに描くか
		std::shared_ptr<VulkanModel> owner; // モデルの部品ならそのモデル(swap()でボーン行列を現在の姿勢にし、その枠用のboneSetを決める)
		uint32_t partIndex = 0;                   // モデルの部品なら、モデルの中での部品の番号(スキニング済みの頂点バッファの選択に使う)
		uint32_t materialIndex = 0;               // モデルの部品なら材質の番号(スキニング用のシェーダーがgl_InstanceIndexで受け取る。firstInstanceで渡す)
		VkDescriptorSet textureSetOverride = VK_NULL_HANDLE; // モデルの部品が複数テクスチャの組を持つとき、textureの代わりにバインドするset
		VkDescriptorSet boneSet = VK_NULL_HANDLE; // LitSkinnedのみ: ボーン行列(swap()でその枠用のものを入れる)
		std::array<float, 4> uvRect = {0.0f, 0.0f, 1.0f, 1.0f}; // スプライトのみ: テクスチャの貼る範囲(u0, v0, 幅, 高さ)
	};
	std::vector<DrawCommand> pending_; // 次のswap()で描画する予約
	std::shared_ptr<VulkanMesh> quadScreen_; // 2D用の板: (0,0)-(1,1)、左上がuv(0,0)
	std::shared_ptr<VulkanMesh> quadWorld_;  // 3D用の板: 中心原点の1x1、+Z向き、左上がuv(0,0)
	float screenWidth_ = 1920.0f;
	float screenHeight_ = 1080.0f;
	// uniform bufferの中身(std140: vec4が5つ+点光源のvec4配列3組(各4個)+mat4+vec4*2=384B。res/shaders/triangle.fragのUboと同じ並び)
	struct UniformData
	{
		std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 1.0f};
		std::array<float, 4> lightDir{0.0f, -1.0f, 0.0f, 0.0f};
		std::array<float, 4> lightColor{1.0f, 1.0f, 1.0f, 1.0f};
		std::array<float, 4> ambient{0.2f, 0.2f, 0.2f, 1.0f};
		std::array<float, 4> cameraPos{0.0f, 0.0f, 0.0f, 1.0f};
		// 点光源(配列。std140ではvec4の配列は16B刻み)。色が黒なら無効
		std::array<std::array<float, 4>, kMaxPointLights> pointPos{};
		std::array<std::array<float, 4>, kMaxPointLights> pointColor{};
		std::array<std::array<float, 4>, kMaxPointLights> pointAtten{}; // 減衰係数(c,l,q)、w=影を落とすか
		std::array<float, 16> lightViewProj{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // 影用(swap()で毎フレーム計算)
		std::array<float, 4> pointShadow{30.0f, 0.1f, 0.0f, 0.0f}; // 点光源の影: 遠クリップ距離, 近クリップ距離, 有効な点光源の数(swap()で計算), トゥーンシェーディング(setToonShading)
		std::array<float, 4> shadowParams{2.0f, 1.5f, 2.0f, 0.0f}; // 影の品質: 平行光源のぼかし半径(テクセル), 法線オフセット(テクセル), 点光源のぼかし半径(テクセル), 平行光源の1テクセルの大きさ(swap()で計算)
		std::array<float, 4> ambientGround{0.2f, 0.2f, 0.2f, 1.0f}; // 半球の環境光の下側(地面からの照り返し)。上側はambient
		std::array<float, 4> envParams{0.0f, 0.0f, 0.0f, 0.0f};     // 床の高さ, ハーフランバートのまわり込み, 接地AOの強さ, 半球の環境光などを使うか
		UniformData()
		{
			for(auto& a : pointPos){ a = {0.0f, 0.0f, 0.0f, 1.0f}; }
			for(auto& c : pointColor){ c = {0.0f, 0.0f, 0.0f, 1.0f}; } // 既定は全部無効
			for(auto& t : pointAtten){ t = {1.0f, 0.35f, 0.44f, 1.0f}; }
		}
	};
	UniformData uniform_;
	std::shared_ptr<VulkanTexture> texture_; // draw()の既定テクスチャ
	float defaultSpecular_ = 0.5f;
	float defaultShininess_ = 32.0f;
	std::shared_ptr<VulkanTexturePool> texturePool_;
	std::unordered_map<std::string, std::weak_ptr<VulkanTexture>> textureCache_;
	std::shared_ptr<VulkanBonePool> bonePool_;
	VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE; // set0: フレームごとのUBO
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
	std::vector<VkSemaphore> renderFinished_; // swapchainイメージごと
	VkCommandPool commandPool_ = VK_NULL_HANDLE;
	// フレームごとのリソース。GPUが前フレームを処理中でもCPUが次フレームを記録できるよう、
	// kMaxFramesInFlight組を持つ。次に使う組はinFlightフェンスで完了を待つ。
	// (renderFinished_はswapchainイメージ単位、深度バッファは全フレームで共有:
	//  render passのsubpass dependencyでフレーム間の書込み順を保証している)
	static constexpr uint32_t kMaxFramesInFlight = kFrameSlots;
	struct Frame
	{
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkSemaphore imageAvailable = VK_NULL_HANDLE;
		VkFence inFlight = VK_NULL_HANDLE;
		VkQueryPool queryPool = VK_NULL_HANDLE; // GPU計測用のタイムスタンプ(kTimestampCount個)
		bool queryPending = false; // queryPoolに未回収の計測結果があるか
		std::array<bool, kMaxPointLights> pointShadowValid{}; // 点光源のシャドウマップを一度でも描いたか(間引きの判定用)
		bool dirShadowValid = false; // 平行光源のシャドウマップを一度でも描いた(クリアした)か。無効の間は、描いた後はパスを省く
		uint32_t pointShadowUses = 0; // この枠を使った回数(間引きの周期)
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		VkImage shadowImage = VK_NULL_HANDLE;
		VkDeviceMemory shadowMemory = VK_NULL_HANDLE;
		VkImageView shadowView = VK_NULL_HANDLE;
		VkFramebuffer shadowFramebuffer = VK_NULL_HANDLE;
		// 点光源のシャドウマップ: 全光源分を1枚のキューブマップ配列(層 = 光源の番号*6 + 面)にまとめた画像。
		// 描画用に層ごとの2Dビューとフレームバッファ、サンプリング用にCUBE_ARRAYビューを持つ
		static constexpr int kPointShadowLayers = kMaxPointLights * 6;
		VkImage pointShadowImage = VK_NULL_HANDLE;
		VkDeviceMemory pointShadowMemory = VK_NULL_HANDLE;
		VkImageView pointShadowArrayView = VK_NULL_HANDLE;
		std::array<VkImageView, kPointShadowLayers> pointShadowLayerViews{};
		std::array<VkFramebuffer, kPointShadowLayers> pointShadowFramebuffers{};
		std::unique_ptr<VulkanBuffer> uniformBuffer; // UniformData(384B)
		std::vector<std::shared_ptr<VulkanMesh>> meshes; // この枠で描画中のメッシュ(fence完了まで保持)
		std::vector<std::shared_ptr<VulkanTexture>> textures; // 同、テクスチャ
		std::vector<std::shared_ptr<VulkanModel>> models; // 同、モデル(ボーン行列のバッファを使っている)
	};
	std::array<Frame, kMaxFramesInFlight> frames_;

	// 描画の計測(環境変数VULKAN_PROFILEが設定されているときだけ、約1秒ごとに平均をログへ出す)。
	// GPU側はタイムスタンプで区間ごとに測り、CPU側はswap()内の各段階の時間を測る
	enum TimestampPoint { kTsStart, kTsSkin, kTsDirShadow, kTsPointShadow, kTsOutline, kTsMain, kTsEnd, kTimestampCount };
	struct ProfileStats
	{
		double gpuMs[kTimestampCount - 1] = {};	// 区間: スキニング / 平行光影 / 点光源影 / 輪郭線 / 本体 / 後処理
		double cpuWaitMs = 0.0;	// フェンス待ち(大きければGPU律速)
		double cpuAcquireMs = 0.0;	// スワップチェーンのイメージ取得待ち(垂直同期)
		double cpuOutsideMs = 0.0;	// swap()の外(シーンの更新など)の時間
		double cpuPrepareMs = 0.0;	// スキニング等の準備
		double cpuRecordMs = 0.0;	// コマンド記録
		double cpuSubmitMs = 0.0;	// 送信+present
		int frames = 0;	// 蓄積したフレーム数
		int gpuFrames = 0;	// GPU計測を蓄積したフレーム数
		int drawCalls = 0;	// 直近フレームの描画予約数
		int litDrawn = 0;	// 直近フレームで描いたライティング付きの数
		int opaqueDrawn = 0;	// そのうち不透明の版のパイプラインで描いた数
		int culled = 0;	// 直近フレームのカリング数(メインのパス)
		int shadowCulled = 0;	// 直近フレームの影のパスでのカリング数(全パスの合計)
		uint64_t lastLog = 0;	// 前回出力時のSDL_GetTicks()
	};
	bool profile_ = false;
	uint32_t skinMaxGroupsX_ = 65535; // スキニングのdispatchの、1次元目のワークグループ数の上限(デバイスの上限)
	float timestampPeriod_ = 0.0f; // タイムスタンプ1目盛りのナノ秒。0なら計測不可
	uint64_t timestampMask_ = 0;
	ProfileStats stats_;
	uint64_t lastSwapEnd_ = 0; // 前回のswap()が終わったSDL_GetPerformanceCounter()
	uint32_t frameIndex_ = 0;
	VkClearColorValue clearColor_{{0.0f, 0.0f, 0.0f, 1.0f}};
	bool swapchainDirty_ = false;
	bool ready_ = false;
};

} // SDL_

#endif // SDLVULKANWINDOW_H_
