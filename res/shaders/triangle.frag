#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
// x=鏡面反射の強さ, y=光沢度, z=フラグ(bit0=影を受けるか、bit1〜7=自発光する点光源の番号+1(0なら通常)、bit8〜=環境光に足す明るさ*100)、w=不透明度。オブジェクトごと
layout(location = 4) flat in vec4 fragMaterial;
// MToon(VRMのトゥーン材質。スキニング付きの頂点シェーダーが、材質ごとのデータから渡す。fragMt3.wが0なら通常のライティング):
//   fragMt0 = (影の色rgb, 影の境目のずらし)、fragMt1 = (影の境目のぼかし, GI均一化, リムの混ぜ具合, リムの鋭さ)、
//   fragMt2 = (リムの色rgb, リムの持ち上げ)、fragMt3 = (発光rgb, フラグ: ビット0=MToonか、ビット1=影の色のテクスチャあり、ビット2=発光のテクスチャあり)
layout(location = 5) flat in vec4 fragMt0;
layout(location = 6) flat in vec4 fragMt1;
layout(location = 7) flat in vec4 fragMt2;
layout(location = 8) flat in vec4 fragMt3;
layout(location = 0) out vec4 outColor;

const int MAX_POINT_LIGHTS = 4;
// 影のぼかしのサンプル数(ポワソンディスクの点の数)。減らすほど軽いが、ぼかしの粒(ノイズ)が目立つ
const int POINT_TAPS = 8;
const int DIR_TAPS = 16; // 平行光源の影のぼかしのサンプル数

// フレームごとの定数(uniform buffer, std140)。VulkanWindowのUniformDataと同じレイアウト
layout(set = 0, binding = 0) uniform Ubo {
	vec4 tint;          // 色に掛ける係数
	vec4 lightDir;      // 平行光源の向き(光が進む方向。ワールド空間、正規化済み)
	vec4 lightColor;    // 平行光源の色
	vec4 ambient;       // 環境光
	vec4 cameraPos;     // カメラ位置(ワールド空間)
	vec4 pointPos[MAX_POINT_LIGHTS];   // 点光源の位置(ワールド空間)
	vec4 pointColor[MAX_POINT_LIGHTS]; // 点光源の色(黒なら無効)
	vec4 pointAtten[MAX_POINT_LIGHTS]; // x,y,z=減衰係数: 1 / (x + y*d + z*d^2)、w=影を落とすか(1/0)
	mat4 lightViewProj; // 平行光源から見たビュー射影(シャドウマップ用)
	vec4 pointShadow;   // 点光源のシャドウマップ: x=遠クリップ距離, y=近クリップ距離, z=有効な点光源の数, w=トゥーンシェーディング(1/0)
	vec4 shadowParams;  // 影の品質: x=平行光源のぼかし半径(テクセル), y=法線方向オフセット(テクセル), z=点光源のぼかし半径(テクセル),
	                    //          w=平行光源のシャドウマップ1テクセルのワールドでの大きさ
	vec4 ambientGround; // 半球の環境光の下側(地面からの照り返し)の色。上側はambient
	vec4 envParams;     // x=床の高さ(接地のAO用), y=ハーフランバートのまわり込み(0で無し), z=接地AOの強さ, w=半球の環境光/まわり込み/AOを使うか(1/0)
} ubo;

// 平行光源のシャドウマップ(デプス)。フレームごとに別の画像
layout(set = 0, binding = 1) uniform sampler2D shadowMap;

// 点光源ごとのシャドウマップ(キューブマップのデプス。6面を光源位置から90度で描いたもの)を、光源の番号を層とする
// キューブマップ配列にまとめたもの。フレームごとに別の画像。層(光源の番号)は texture() の4成分目で指定する
layout(set = 0, binding = 2) uniform samplerCubeArray pointShadowMaps;

layout(set = 1, binding = 0) uniform sampler2D tex;
// MToonの影の色・発光のテクスチャ(無い材質では、同じ基本の画像がバインドされる。使うかどうかはfragMt3.wのフラグで決める)
layout(set = 1, binding = 1) uniform sampler2D shadeTex;
layout(set = 1, binding = 2) uniform sampler2D emissiveTex;

// 半球の環境光(IBLの代わり): 法線が上を向くほど空の色(ambient)、下を向くほど地面からの照り返し(ambientGround)。
// 接地AO: 床に近く、下を向いた面ほど暗くする(壁・床との隙間で光が届きにくいことの近似)
vec3 hemisphereAmbient(vec3 n, vec3 worldPos)
{
	if (ubo.envParams.w < 0.5) {
		return ubo.ambient.rgb;
	}
	vec3 hemi = mix(ubo.ambientGround.rgb, ubo.ambient.rgb, n.y * 0.5 + 0.5);
	float height = max(worldPos.y - ubo.envParams.x, 0.0);
	float ao = 1.0 - ubo.envParams.z * clamp(1.0 - height / 0.6, 0.0, 1.0) * clamp(0.5 - n.y * 0.5, 0.0, 1.0);
	return hemi * ao;
}

// ぼかし用の点(単位円内に均等に散らしたポワソンディスク16点)
const vec2 POISSON[16] = vec2[](
	vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
	vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464), vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
	vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
	vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590), vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790));

// ピクセルごとにずらす回転角(0〜2π)。同じ並びの点がそろって縞(バンディング)に見えるのを、細かいノイズに変えて目立たなくする
float noiseAngle()
{
	return 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
}

// 平行光源に対する影の係数(1=照らされる、0=影)。ポワソンディスク16点でぼかす。
// 法線方向オフセット: 参照する位置を面の法線方向へ少しずらして、斜めの面の自己影(shadow acne)を防ぐ。
// 深度バイアスだけで防ぐと影が物体から浮く(peter panning)ので、バイアスは最小限にしている
float shadowFactor(vec3 worldPos, vec3 n, float nDotL)
{
	if (ubo.lightDir.w < 0.5) {
		return 1.0; // シャドウマップ無効(VulkanWindow::setShadowMapsEnabled)
	}
	float sinTheta = sqrt(max(1.0 - nDotL * nDotL, 0.0));
	vec3 offsetPos = worldPos + n * (ubo.shadowParams.y * ubo.shadowParams.w * sinTheta);
	vec4 lightPos = ubo.lightViewProj * vec4(offsetPos, 1.0);
	vec3 p = lightPos.xyz / lightPos.w; // 平行投影なのでw=1
	// クリップ空間(y下向き)→テクスチャ座標(v下向き)は x,y とも 0.5倍+0.5
	vec2 uv = p.xy * 0.5 + 0.5;
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) {
		return 1.0; // シャドウマップの範囲外は影なし
	}
	float bias = 0.0003;
	vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
	float angle = noiseAngle();
	mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
	float lit = 0.0;
	for (int i = 0; i < DIR_TAPS; ++i) {
		vec2 offset = rotation * POISSON[i] * ubo.shadowParams.x * texel;
		float closest = texture(shadowMap, uv + offset).r;
		lit += (p.z - bias <= closest) ? 1.0 : 0.0;
	}
	return lit / float(DIR_TAPS);
}

float samplePointShadow(int light, vec3 dir)
{
	return texture(pointShadowMaps, vec4(dir, float(light))).r;
}

// 点光源lightに対する影の係数(1=照らされる、0=影)。
// キューブマップには「面の軸方向の距離d」の透視深度が入っている(createCubeFaceViewProjと同じ式)ので、
// 線形の距離に戻して比べる。サンプル方向の面ごとに軸が違うため、フラグメントの距離もその面の軸で取る。
// 法線方向オフセットとポワソンディスク12点のぼかし(方向を接線方向にずらす)は、平行光源と同じ考え方
float pointShadowFactor(int light, vec3 worldPos, vec3 n, float nDotL)
{
	if (ubo.lightDir.w < 0.5) {
		return 1.0; // シャドウマップ無効
	}
	if (ubo.pointAtten[light].w < 0.5) {
		return 1.0; // この光源は影を落とさない設定
	}
	float farZ = ubo.pointShadow.x;
	float nearZ = ubo.pointShadow.y;
	// 1テクセルの大きさ(ワールド): 90度の面なので距離dで 2*d/解像度
	float texelAngle = 2.0 / float(textureSize(pointShadowMaps, 0).x);
	float sinTheta = sqrt(max(1.0 - nDotL * nDotL, 0.0));
	vec3 lightPos = ubo.pointPos[light].xyz;
	vec3 toFrag = worldPos - lightPos;
	float dist = length(toFrag);
	if (dist >= farZ) {
		return 1.0;
	}
	toFrag += n * (ubo.shadowParams.y * texelAngle * dist * sinTheta); // 法線方向オフセット
	dist = length(toFrag);
	vec3 dirN = toFrag / dist;
	vec3 helper = abs(dirN.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(helper, dirN));
	vec3 bitangent = cross(dirN, tangent);
	float spread = ubo.shadowParams.z * texelAngle * dist; // ぼかし半径(ワールド)
	float bias = 0.01; // 距離の単位(ワールド)でのバイアス(法線オフセットがあるので小さくてよい)
	float angle = noiseAngle();
	mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));

	float lit = 0.0;
	for (int i = 0; i < POINT_TAPS; ++i) {
		vec2 offset = rotation * POISSON[i] * spread;
		vec3 sampleDir = toFrag + tangent * offset.x + bitangent * offset.y;
		vec3 a = abs(sampleDir);
		// この面の軸方向の、フラグメント自身の距離
		vec3 aFrag = abs(toFrag);
		float fragDist = (a.x >= a.y && a.x >= a.z) ? aFrag.x : ((a.y >= a.z) ? aFrag.y : aFrag.z);
		float stored = samplePointShadow(light, sampleDir);
		float storedDist = (nearZ * farZ) / (farZ - stored * (farZ - nearZ)); // 透視深度→軸方向の距離
		lit += (fragDist - bias <= storedDist) ? 1.0 : 0.0;
	}
	return lit / float(POINT_TAPS);
}

// Blinn-Phong: 拡散(ランバート)と鏡面(ハーフベクトル)を返す。Lは光源へ向かう単位ベクトル
void blinnPhong(vec3 n, vec3 v, vec3 l, out float diffuse, out float specular)
{
	diffuse = max(dot(n, l), 0.0);
	vec3 h = normalize(l + v);
	// 光が当たらない面(diffuse==0)には鏡面反射を出さない
	specular = diffuse > 0.0 ? pow(max(dot(n, h), 0.0), fragMaterial.y) : 0.0;
}

// トゥーンシェーディング: 明るさ(0〜1)を3段(影/中間/明)に分ける。段の境目は少しだけぼかして、ギザギザを目立たなくする
float toonBand(float x)
{
	return 0.5 * smoothstep(0.18, 0.22, x) + 0.5 * smoothstep(0.50, 0.54, x);
}

// MToonの境目: 明るさtを、ぼかしtoonyに応じた範囲で0〜1にする(toonyが1に近いほど、くっきり)
float linearStep(float a, float b, float t)
{
	return clamp((t - a) / max(b - a, 1e-4), 0.0, 1.0);
}

// MToonの陰影: 明るい色(base)と影の色(基本色のテクスチャ x 影の色)を、光の当たり方(境目をぼかしの範囲で切り替え)で混ぜる。
// 平行光源と点光源のそれぞれに同じ式を使い、環境光・リム・発光を足す。
// VRChat/VRoid Hubのビューアのように、全体が明るく、顔色が悪く見える影を作らない見た目にする:
//  - 光が十分に当たる面は、基本色そのままの明るさになるよう、光の強さで正規化する(シーンの光が弱くてもアバターが暗くならない)
//  - 陰の面も、影の色と環境光(基本色)の混ざりで、極端に暗くしない
//  - 肌の材質は、シャドウマップの影(髪・顎などの落ち影)を受けない。顔や首に斑な影が出ないようにする(服・髪は受ける)
vec3 mtoon(vec4 base, vec3 n, vec3 v, bool receiveShadow, float ambientBoost)
{
	float shift = fragMt0.w;
	float toony = fragMt1.x;
	vec3 lit = base.rgb;
	int mtoonFlags = int(fragMt3.w + 0.5);
	// 影の色: 影の色のテクスチャ(無ければ基本色のテクスチャ=base) x 影の色の係数
	vec3 shade = ((mtoonFlags & 2) != 0 ? texture(shadeTex, fragUv).rgb * ubo.tint.rgb : base.rgb) * fragMt0.rgb;
	bool skin = (mtoonFlags & 8) != 0;
	bool shadowed = receiveShadow && !skin;

	float dNL = dot(n, -ubo.lightDir.xyz);
	float shading = linearStep(-1.0 + toony, 1.0 - toony, dNL + shift);
	if (shadowed && dNL > -0.5) {
		shading *= shadowFactor(fragWorldPos, n, max(dNL, 0.0));
	}
	vec3 direct = ubo.lightColor.rgb * mix(shade, lit, shading);

	int count = clamp(int(ubo.pointShadow.z + 0.5), 0, MAX_POINT_LIGHTS);
	for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
		if (i >= count) {
			break;
		}
		vec3 toLight = ubo.pointPos[i].xyz - fragWorldPos;
		float dist = length(toLight);
		float atten = 1.0 / (ubo.pointAtten[i].x + ubo.pointAtten[i].y * dist + ubo.pointAtten[i].z * dist * dist);
		vec3 l = toLight / dist;
		float pNL = dot(n, l);
		if (pNL <= 0.0) {
			continue; // 光の当たらない向き: 寄与は max(pNL, 0) 倍なので0。影の参照(PCF)も省く
		}
		float pShading = linearStep(-1.0 + toony, 1.0 - toony, pNL + shift);
		if (shadowed) {
			pShading *= pointShadowFactor(i, fragWorldPos, n, max(pNL, 0.0));
		}
		// 点光源は、通常のライティングと同じ明るさになるよう、光の当たる向き(N・L)の分だけ弱める。影の色との混ぜ方はMToonの式
		direct += ubo.pointColor[i].rgb * atten * max(pNL, 0.0) * mix(shade, lit, pShading);
	}

	// 環境光(GI)は向きによらず一様。明るい色で受ける
	vec3 indirect = lit * (hemisphereAmbient(n, fragWorldPos) * 0.8 + ambientBoost);

	// 光の強さの正規化: 平行光源の色 + 環境光で、基本色がちょうど1倍になるようにする(点光源・リム・発光は足すだけ)
	vec3 norm = 1.0 / max(ubo.lightColor.rgb + ubo.ambient.rgb * 0.8, vec3(0.2));
	vec3 color = (direct + indirect) * norm;

	// パラメトリックなリム: 輪郭ぎわを光らせる。光(平行光源+環境光)の明るさと混ぜる割合(fragMt1.z)で効きを変える
	float rimFactor = pow(clamp(1.0 - dot(n, v) + fragMt2.w, 0.0, 1.0), max(fragMt1.w, 1e-3));
	color += fragMt2.rgb * rimFactor * mix(vec3(1.0), ubo.lightColor.rgb + ubo.ambient.rgb, fragMt1.z);

	// 発光: 係数 x 発光のテクスチャ(無ければ係数だけ)
	color += fragMt3.rgb * ((mtoonFlags & 4) != 0 ? texture(emissiveTex, fragUv).rgb : vec3(1.0));
	return color;
}

void main()
{
	int flags = int(fragMaterial.z + 0.5);
	bool receiveShadow = (flags & 1) != 0;
	int emissiveLight = ((flags >> 1) & 127) - 1; // 負なら自発光しない
	float ambientBoost = float((flags >> 8) & 255) * 0.01;
	int cutoffPercent = flags >> 16; // アルファの切り抜きのしきい値*100(0なら切り抜き無し)

	// 自発光(点光源の目印用): 光源計算をせず、その点光源の色で描く
	if (emissiveLight >= 0) {
		outColor = vec4(ubo.pointColor[min(emissiveLight, MAX_POINT_LIGHTS - 1)].rgb, 1.0);
		return;
	}

	// 両面描画なので、裏面を見ている場合は法線を反転する
	vec3 n = normalize(fragNormal);
	if (!gl_FrontFacing) {
		n = -n;
	}
	vec3 v = normalize(ubo.cameraPos.xyz - fragWorldPos);

	// 平行光源
	float dDiffuse, dSpecular;
	blinnPhong(n, v, -ubo.lightDir.xyz, dDiffuse, dSpecular);
	// 影は拡散・鏡面だけに掛ける(環境光には掛けない)
	bool toon = ubo.pointShadow.w > 0.5;
	// ハーフランバート風のまわり込み(トゥーン以外): 光の当たらない側へ少しだけ光を回して、陰を硬くしすぎない。
	// 皮膚の透過の近似として、回り込んだ部分は赤みを帯びる
	float wrap = (!toon && ubo.envParams.w > 0.5) ? ubo.envParams.y : 0.0;
	float dNL = dot(n, -ubo.lightDir.xyz);
	float dWrapped = wrap > 0.0 ? clamp((dNL + wrap) / (1.0 + wrap), 0.0, 1.0) : dDiffuse;
	vec3 dTint = wrap > 0.0 ? mix(vec3(1.0), vec3(1.0, 0.62, 0.52), (1.0 - clamp(dNL, 0.0, 1.0)) * 0.8) : vec3(1.0);
	float shadow = (receiveShadow && dWrapped > 0.0) ? shadowFactor(fragWorldPos, n, max(dDiffuse, 0.05)) : 1.0;
	// トゥーンでは、拡散を3段に、鏡面を「ある/なし」の2段にする(影の中は鏡面なし)
	float dLit = toon ? toonBand(dDiffuse * shadow) : dWrapped * shadow;
	float dSpec = toon ? smoothstep(0.45, 0.5, dSpecular) * step(0.5, shadow) : dSpecular * shadow;
	vec3 diffuse = hemisphereAmbient(n, fragWorldPos) + ambientBoost + ubo.lightColor.rgb * dTint * dLit;
	vec3 specularLight = ubo.lightColor.rgb * dSpec;

	// 点光源(距離による減衰と、光源ごとの影あり)
	int count = clamp(int(ubo.pointShadow.z + 0.5), 0, MAX_POINT_LIGHTS);
	for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
		if (i >= count) {
			break;
		}
		vec3 toLight = ubo.pointPos[i].xyz - fragWorldPos;
		float dist = length(toLight);
		float atten = 1.0 / (ubo.pointAtten[i].x + ubo.pointAtten[i].y * dist + ubo.pointAtten[i].z * dist * dist);
		float pDiffuse, pSpecular;
		blinnPhong(n, v, toLight / dist, pDiffuse, pSpecular);
		float pNL = dot(n, toLight / dist);
		float pWrapped = wrap > 0.0 ? clamp((pNL + wrap) / (1.0 + wrap), 0.0, 1.0) : pDiffuse;
		float pShadow = (receiveShadow && pWrapped > 0.0) ? pointShadowFactor(i, fragWorldPos, n, max(pDiffuse, 0.05)) : 1.0;
		float pLit = toon ? toonBand(pDiffuse * pShadow) : pWrapped * pShadow;
		float pSpec = toon ? smoothstep(0.45, 0.5, pSpecular) * step(0.5, pShadow) : pSpecular * pShadow;
		diffuse += ubo.pointColor[i].rgb * pLit * atten;
		specularLight += ubo.pointColor[i].rgb * pSpec * atten;
	}

	// 鏡面反射は表面色ではなく光の色で光る(白いハイライト)
	vec3 specular = fragMaterial.x * specularLight;

	vec4 base = texture(tex, fragUv) * vec4(fragColor, fragMaterial.w) * ubo.tint;
	// ほぼ透明な部分は描かない(深度も書かない)。半透明の部分はアルファブレンドされる
#if defined(MASK_PASS)
	// アルファの切り抜き(MASK)の材質用の版(triangle_mask.frag.spv。-DMASK_PASS): しきい値未満を描かず、残りは不透明として出力する
	if (base.a < float(cutoffPercent) * 0.01) {
		discard;
	}
	base.a = 1.0;
#elif defined(OPAQUE_PASS)
	// 不透明の材質用の版(triangle_opaque.frag.spv。CMakeが -DOPAQUE_PASS でコンパイル): アルファの切り抜き・ブレンドが無いので discard せず、
	// 不透明として出力する。discard が無いと、TBDRのGPUで隠れた面の描画(重なった服・髪などの無駄なシェーディング)を省ける
	base.a = 1.0;
#else
	if (base.a < 0.05) {
		discard;
	}
#endif
	if (fragMt3.w > 0.5) {
		outColor = vec4(mtoon(base, n, v, receiveShadow, ambientBoost), base.a);
		return;
	}
	outColor = vec4(base.rgb * diffuse + specular, base.a);
}
