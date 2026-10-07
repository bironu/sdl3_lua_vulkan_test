-- 地面のタイルの種類(フィールドエディタ・ゲームが共通で読む)。上から順に、番号0、1、2...。
-- id: 名前、symbol: フィールドのファイル(res/field/*.lua)での1文字、color: {R, G, B}(0〜255)、name: 表示名の多言語のキー(lang/*.lua)
-- walkable: 省略すると歩ける。false にすると、そのタイルの上は歩けない(水など)
-- 並びを変えても、フィールドのファイルは記号で持っているので壊れない(新しいタイルは、記号が重ならないよう末尾へ足すこと)。0番が、新しいフィールドの初期値
tiles = {
	{id = "grass",   symbol = "g", color = {96, 160, 72},   name = "TileGrass"},
	{id = "soil",    symbol = "s", color = {150, 112, 78},  name = "TileSoil"},
	{id = "asphalt", symbol = "a", color = {70, 72, 78},    name = "TileAsphalt"},
	{id = "stone",   symbol = "t", color = {170, 168, 160}, name = "TileStone"},
	{id = "sand",    symbol = "d", color = {218, 200, 150}, name = "TileSand"},
	{id = "water",   symbol = "w", color = {70, 130, 190},  name = "TileWater", walkable = false},
}
