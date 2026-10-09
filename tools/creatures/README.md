# モブ(敵)のモデルとモーション

ゲーム本体は、敵のモデルもモーションも、ファイル(glb)から読む。生成器は持たない。
新しいモブは、glb を1つ用意して、`res/lua/data/enemies.lua` に1項目足せば出せる。

## (a) ツールで作る

丸い生き物の形と動きを、数値から作って glb に書き出す(`tools/creature/` の生成器と数式。ツール creature2glb だけが使う)。

1. `tools/creatures/hopper.lua`(4本足)か `crawler.lua`(6本足)を写して、`creature = { ... }` の数値を変える
   (形: `body` `face` `legs` `color`、大きさ: `length`、動き: `walkFast` `walkSlow` `idle` `attackStand` `attackJump` `death`)。
   足は何対でもよい(付け根の前後の位置で、前足・後ろ足を決める。3対なら三脚歩行)。
2. 書き出す(パスはリポジトリ直下から):
   ```
   cmake --build build --target creature2glb
   ./dist/creature2glb tools/creatures/hopper.lua res/model/enemy/hopper.glb
   ```
   書き出した後、エンジンの読み込みで読み直して照合する(合わなければ終了コード1)。
3. ログに出る `strides for enemies.lua motions` と `phases for enemies.lua motions` の値を、enemies.lua の `motions` に書く。

## (b) Blender などで自作する場合の glb の条件

- スキン付きのメッシュ(glTF 2.0。`.glb` か `.gltf`)。材質は基本色だけでよい(テクスチャも可)。
- ボーンは1本の階層。根(親の無いボーン。例: `root`)の下に、体・手足を自由に付ける。ボーンの数・名前は任意
  (ゲームは名前でクリップのトラックとボーンを対応づけるだけで、特定の構造を前提にしない)。
- アニメーション(クリップ)の名前は任意(enemies.lua の `motions` で対応づける)。必要な動き: 待機・早歩き・ゆっくり歩き・攻撃A(倒れ込み)・攻撃B(跳びかかり)・倒れ。
  待機と歩きはループ(最初と最後が同じ姿勢。歩きは1周期)。
- クリップは「その場」の姿勢だけ: 根のボーンの平行移動は当てない(前進・跳ぶ高さ・距離はゲームが決める)。
  攻撃Bは、溜め → 踏み切り → 空中 → 着地 → 戻り を、その場で作る(空中の区間は、滞空時間に合わせて伸縮して再生される)。
- 根のボーンの拡大縮小 = 体全体の潰れ・伸び(地面が基準)。ほかのボーンの拡大縮小は使わない。
- 休止ポーズでも、各クリップでも、足が地面 Y=0 に着くように作る(ゲームは、休止ポーズのメッシュのいちばん低い点を地面に合わせる)。
- 前は +Z(glTF の座標。Blender の書き出しで「+Y上」をオン。Blender では -Y が前)、上は +Y。単位はメートル。
- 書き出し: ファイル > エクスポート > glTF 2.0(.glb)、アニメーションの「モード」を「アクション」にして、全部のアクションを書き出す。

## (c) enemies.lua に1項目足す

```lua
{
	name = "crawler",
	model = "res/model/enemy/crawler.glb", -- clips を書かなければ、クリップも同じファイルから
	count = 100, seed = 2,
	-- length = 0.8, box = { width = …, length = …, height = …, fallLength = 0.25 }, shadowRadius = …,
	--   (無ければ、メッシュの範囲から求める。length を書くと、メッシュの前後の長さをその長さ(m)に拡大縮小する)
	motions = {   -- ゲームの動き → クリップの名前と区間(秒。クリップの時刻)
		idle = { clip = "idle", loop = true },
		walkFast = { clip = "walkFast", loop = true, stride = 0.36 }, -- stride: クリップの1周で進む距離(m)
		walkSlow = { clip = "walkSlow", loop = true, stride = 0.4 },
		attackStand = { clip = "attackStand", fallStart = 1.2, fallEnd = 1.55, recoverStart = 2.05, recoverEnd = 2.65 },
		attackJump = { clip = "attackJump", crouchEnd = 0.35, launchEnd = 0.43, airEnd = 0.93 },
		death = { clip = "death" },
	},
	jump = { height = 0.7, distance = 2.5, … },          -- 跳ぶ軌道(ゲームが決める)
	behavior = { fastSpeed = 2.2, slowSpeed = 1.0, … },  -- 歩く速さ・攻撃の選び方
},
```

- 区間の秒: 攻撃A は倒れ込み(`fallStart`〜`fallEnd`)と起き上がり(`recoverStart`〜`recoverEnd`)。
  攻撃B は溜めの終わり(`crouchEnd`)・踏み切りの終わり(`launchEnd`)・空中の終わり(`airEnd`)。
  Blender でタイミングを変えたら、ここも直す。
- 数・速さ・止まる距離・出現・攻撃の確率などの項目は、enemies.lua の hopper の項目のコメントを参照。ゲームの中で F5 を押すと読み直す。
