#version 450

// 後処理用の全画面の三角形(頂点バッファなし。gl_VertexIndex 0,1,2 で画面全体を覆う)
void main()
{
	vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
