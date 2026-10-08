-- フィールドの設定(ゲーム本体とフィールドエディタが共通で読む)。ファイル名を変えるときは、ここだけ直す
settings = {
	tiles = "res/lua/data/field_tiles.lua", -- タイルの種類の定義
	maxSlope = 0.85,                        -- 歩いて登れる勾配の上限(高さ/水平の距離。0.85で約40度)
	maxClimbSlope = 1.6,                    -- maxSlopeより急な坂のうち、ゆっくり登れる勾配の上限(約58度)。これより急な所へは進めない
	field = "res/field/field01.fld",        -- ゲームが読み込む(エディタが保存する)フィールド(バイナリ)。置物のモデルは res/prop/
}
