#version 450

// 後処理: シーンの色(sceneColor_)をスワップチェーンへ写す。pc.z > 0.5ならFXAAを掛ける。
// FXAAはNVIDIAのFXAA(Timothy Lottes)の簡易版: 輝度から求めたエッジの向きに沿って数回サンプルして混ぜる
layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(push_constant) uniform Push {
	vec4 params;  // xy=シーンの1テクセルの大きさ(uv)、z=FXAAを掛けるか
	vec4 params2; // xy=スワップチェーンの1ピクセルの大きさ(uv)
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
	vec2 rcp = pc.params.xy;
	vec2 uv = gl_FragCoord.xy * pc.params2.xy; // シーンとスワップチェーンの解像度が違っても、画面全体を覆うuv
	vec3 rgbM = texture(sceneTex, uv).rgb;
	if (pc.params.z < 0.5) {
		outColor = vec4(rgbM, 1.0);
		return;
	}

	const vec3 luma = vec3(0.299, 0.587, 0.114);
	const float REDUCE_MIN = 1.0 / 128.0;
	const float REDUCE_MUL = 1.0 / 8.0;
	const float SPAN_MAX = 8.0;

	vec3 rgbNW = texture(sceneTex, uv + vec2(-1.0, -1.0) * rcp).rgb;
	vec3 rgbNE = texture(sceneTex, uv + vec2( 1.0, -1.0) * rcp).rgb;
	vec3 rgbSW = texture(sceneTex, uv + vec2(-1.0,  1.0) * rcp).rgb;
	vec3 rgbSE = texture(sceneTex, uv + vec2( 1.0,  1.0) * rcp).rgb;
	float lumaNW = dot(rgbNW, luma);
	float lumaNE = dot(rgbNE, luma);
	float lumaSW = dot(rgbSW, luma);
	float lumaSE = dot(rgbSE, luma);
	float lumaM = dot(rgbM, luma);
	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

	vec2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * REDUCE_MUL), REDUCE_MIN);
	float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * rcp;

	vec3 rgbA = 0.5 * (texture(sceneTex, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(sceneTex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
	vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(sceneTex, uv + dir * -0.5).rgb + texture(sceneTex, uv + dir * 0.5).rgb);
	float lumaB = dot(rgbB, luma);
	outColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
