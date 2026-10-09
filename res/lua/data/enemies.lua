-- 敵の定義(GameScene の EnemyHorde が読む。ゲームの中でF5を押すと、読み直して出し直す)。長さはメートル、時間は秒、角度はラジアン。
-- 種類ごとに1つの表。無い項目は、C++側の既定値(include/scene/game/EnemyHorde.h の EnemyType、include/scene/game/CreatureClipAnimator.h の CreatureClipMotion)のまま。
-- 体は、ファイル(model)から読む: glb / gltf(スキン付きのメッシュと、アニメーションのクリップ)か、PMX(下の例)。
-- glb の敵は、motions の表で、ゲームの動き → クリップの名前・区間を対応づけて再生する(ボーンの数・名前は任意)。新しい敵を足す手順は tools/creatures/README.md。
-- 形から決まる値(体長・当たりの箱・影の半径・地面に置く高さ)は、読んだメッシュから求める(length・box・shadowRadius を書けば、その値で上書き)。
-- いまは、プレイヤーへ向かって歩いて、近づくと攻撃の動きをするだけ(プレイヤーとの当たり判定・戦闘は無し。敵同士は箱(box)で重ならない)
enemies = {
	{
		name = "hopper",       -- 丸い4本足(tools/creatures/hopper.lua から creature2glb で書き出したもの)
		model = "res/model/enemy/hopper.glb",
		-- clips = "res/model/enemy/hopper_motions.glb", -- クリップを別のファイルから読むとき(無ければ model と同じファイル。単位はモデルと同じ)
		count = 200,           -- 出す数
		seed = 1,              -- 出す位置・個体差の乱数の種(同じなら、毎回同じ並び)
		length = 1.1,          -- 体長(m。メッシュの前後の長さを、これに合わせて拡大縮小する。無ければ、ファイルの大きさのまま(glb はメートル))
		speedJitter = 0.2,     -- 歩く速さの個体差(±割合。速さは behavior の fastSpeed / slowSpeed)
		turnSpeed = 5.0,       -- 進む向きへ向く速さ(ラジアン/秒)
		spawnMinRadius = 6.0,  -- 出す場所: プレイヤーからこの距離〜spawnRadiusの輪の中に、ばらまく
		spawnRadius = 20.0,    -- (フィールドの外になる分は、縁へ寄せる)
		stopDistance = 1.4,    -- プレイヤーにこの距離まで近づいたら止まる
		stopJitter = 1.0,      -- 止まる距離の個体差(0〜この値を足す。群れが同じ輪に重ならないよう)
		-- 敵同士の当たりの箱(m): 幅(左右)・長さ(前後)・高さ。向きに合わせて回した箱を囲む、軸に平行な箱で、重ならないよう押し出す
		-- (跳んでいる間は判定しない)。fallLength: 攻撃Aで前へ倒れ込んだときに、前へ伸ばす長さ。無い項目(0)は、メッシュの範囲から
		box = { width = 0.9, length = 1.0, height = 0.9, fallLength = 0.35 },
		cameraCullRadius = 2.6, -- カメラから箱までが、この距離より近い敵は描かない(画面を覆わないよう。影も)
		shadowRadius = 0.5,    -- 足もとの丸い影の半径(無ければ、箱の幅と長さの平均の半分)
		shadowOpacity = 0.5,   -- 同、濃さ(0〜1)
		motionBlend = 5.0,     -- 動き(歩き・攻撃・待機)を切り替えるときに、混ぜて移る速さ(1/秒)
		-- 動き → クリップの名前。クリップは「その場」の姿勢だけを持つ: root ボーンの平行移動(ルートモーション)は当てない(前進・跳ぶ高さはゲームが決める。
		-- 当てないボーンを変えるなら rootMotionBones = { "root" } のように書く)。root の拡大縮小(潰れ)は、体全体の大きさとして使う。
		-- loop: 繰り返す。歩きの stride: クリップの1周で進む距離(m。無ければ behavior の fastSpeed / slowSpeed × クリップの長さ)。
		-- 歩きの stride・区間の境(秒。クリップの時刻)は、creature2glb が書き出すときにログに出す値。Blender でタイミングを変えたら、ここも直すこと。
		--   attackStand: 倒れ込み fallStart〜fallEnd、起き上がり recoverStart〜recoverEnd(当たりの箱を前へ伸ばす度合い)
		--   attackJump: 溜め 〜crouchEnd、踏み切り 〜launchEnd、空中 〜airEnd(この区間を、敵が決めた滞空時間に合わせて伸縮して再生する)、その後は着地・戻り
		motions = {
			idle = { clip = "idle", loop = true },
			walkFast = { clip = "walkFast", loop = true, stride = 0.55 },
			walkSlow = { clip = "walkSlow", loop = true, stride = 0.6 },
			attackStand = { clip = "attackStand", fallStart = 1.5, fallEnd = 1.9, recoverStart = 2.5, recoverEnd = 3.2 },
			attackJump = { clip = "attackJump", crouchEnd = 0.45, launchEnd = 0.55, airEnd = 1.15 },
			death = { clip = "death" },
		},
		-- 攻撃B(跳びかかる)の軌道: 踏み切りの直前に、敵が決める: 着地点(landGap など) → 跳ぶ距離 → 頂点の高さ(距離に比例。経路の地面・置物を越える高さまで上げる) →
		-- 滞空時間(√(8 × 高さ ÷ gravity))。越えられない(maxHeight を超える)・着地点に立てないなら、距離を縮め、minDistance より短くなるなら跳ばずに歩く
		jump = {
			height = 0.9,        -- 跳ぶ距離が distance のときの、頂点の高さ(m。距離に比例させる)
			distance = 3.0,      -- 跳ぶ距離の上限(m)
			minDistance = 0.8,   -- 跳ぶ距離の下限(m。経路が塞がれて、これより短くしか跳べないなら、跳ばずに歩く)
			minHeight = 0.4,     -- 頂点の高さの下限(m)
			maxHeight = 2.5,     -- 頂点の高さの上限(m。経路の障害物を越えるのに、これより高く跳ぶ必要があれば、越えられない)
			gravity = 20.0,      -- 重力(m/秒²。滞空時間を決める。高さ 0.9 m で 0.6 秒)
			clearance = 0.2,     -- 経路の置物の上に空ける高さ(m)
			probeStep = 0.25,    -- 経路の地面・置物の高さを調べる間隔(m)
			landGap = 1.2,       -- 着地点の、プレイヤーまでの距離(m)
			landGapJitter = 0.4, -- 同、ばらつき(±m)
			landAngleJitter = 0.6, -- 着地点の、プレイヤーから見た向きのばらつき(±ラジアン。敵のいる向きから回す)
			landTries = 4,       -- 着地点の候補の数(他の敵の箱と重ならない候補を選ぶ。全部重なるなら、重なりのいちばん小さい候補)
		},
		-- 行動: 個体ごとに、fastRatio の割合で早歩き(fastSpeed m/秒)、残りはゆっくり歩き(slowSpeed m/秒)。
		-- プレイヤーまでの距離が closeRange 以下なら攻撃A、jumpMin より遠く jumpRange 以下なら攻撃B を始められる。
		-- 攻撃を試す時期(cooldown + 0〜cooldownJitter 秒ごと)に、standChance・jumpChance の確率で始める(始めなければ retry 秒後にまた試す)。
		-- timeJitter: 待機・攻撃の速さの個体差(±割合)
		behavior = {
			fastRatio = 0.5, fastSpeed = 1.8, slowSpeed = 0.8,
			closeRange = 1.8, jumpMin = 2.0, jumpRange = 4.0,
			standChance = 0.6, jumpChance = 0.5,
			cooldown = 2.5, cooldownJitter = 2.0, retry = 0.6,
			timeJitter = 0.15,
		},
		-- アニメーションの間引き(遠い個体は、姿勢の更新の頻度を落とす): プレイヤーから near までは毎フレーム、far で interval 秒ごと(その間は直線で増やす)
		lod = {
			near = 8.0,
			far = 30.0,
			interval = 0.2,
		},
	},
	{
		name = "crawler",      -- 6本足(tools/creatures/crawler.lua から書き出したもの)。体長・箱・影は、メッシュから求める
		model = "res/model/enemy/crawler.glb",
		count = 100,
		seed = 2,
		speedJitter = 0.2,
		turnSpeed = 6.0,
		spawnMinRadius = 6.0,
		spawnRadius = 20.0,
		stopDistance = 1.2,
		stopJitter = 1.0,
		box = { fallLength = 0.25 },
		cameraCullRadius = 2.6,
		shadowOpacity = 0.5,
		motionBlend = 6.0,
		motions = {
			idle = { clip = "idle", loop = true },
			walkFast = { clip = "walkFast", loop = true, stride = 0.36 },
			walkSlow = { clip = "walkSlow", loop = true, stride = 0.4 },
			attackStand = { clip = "attackStand", fallStart = 1.2, fallEnd = 1.55, recoverStart = 2.05, recoverEnd = 2.65 },
			attackJump = { clip = "attackJump", crouchEnd = 0.35, launchEnd = 0.43, airEnd = 0.93 },
			death = { clip = "death" },
		},
		jump = {
			height = 0.7, distance = 2.5, minDistance = 0.7, minHeight = 0.3, maxHeight = 2.0, gravity = 20.0,
			clearance = 0.2, probeStep = 0.25,
			landGap = 1.0, landGapJitter = 0.4, landAngleJitter = 0.8, landTries = 4,
		},
		behavior = {
			fastRatio = 0.6, fastSpeed = 2.2, slowSpeed = 1.0,
			closeRange = 1.6, jumpMin = 1.8, jumpRange = 3.5,
			standChance = 0.5, jumpChance = 0.6,
			cooldown = 2.0, cooldownJitter = 2.0, retry = 0.5,
			timeJitter = 0.15,
		},
		lod = { near = 8.0, far = 30.0, interval = 0.2 },
	},
	-- PMXのモデルの敵の例(ボーンはMMDの標準の名前。いまは使えるモデルが無い)
	-- {
	-- 	name = "pmx",
	-- 	model = "res/model/…/….pmx",
	-- 	count = 100,
	-- 	height = 1.2,          -- 背の高さ(m。メッシュの高さを、これに合わせて拡大縮小する。PMXは1単位=約8cm なので、書くこと)
	-- 	-- 歩き(PMXにはVRMAを当てられないので、ボーンを周期的に回す)。角度は、歩いているときの最大の振れ幅
	-- 	gait = {
	-- 		stride = 1.3,      -- 1周期(左右1歩ずつ)で進む距離
	-- 		legSwing = 0.45,   -- 脚(太もも)を前後に振る角度
	-- 		kneeBend = 0.8,    -- 脚を前へ振り出すときに、膝を曲げる角度
	-- 		ankle = 0.6,       -- 足首で、脚の傾きを打ち消す割合(1で足の裏が常に水平)
	-- 		armSwing = 0.45,   -- 腕を前後に振る角度(脚と逆の向き)
	-- 		armDown = 0.35,    -- Aポーズの腕を、体の横へ下ろす角度
	-- 		elbowBend = 0.35,  -- 肘を曲げておく角度
	-- 		twist = 0.12,      -- 上半身を、腕の振りに合わせてひねる角度(下半身は逆へ半分)
	-- 		lean = 0.1,        -- 歩くとき、上半身を前へ傾ける角度
	-- 		bob = 0.015,       -- 体の上下の揺れ(m)
	-- 		sway = 0.015,      -- 体の左右の揺れ(m)
	-- 		blend = 4.0,       -- 歩き出し・止まるときに、振れ幅が変わる速さ(1/秒)
	-- 	},
	-- 	-- ボーンの名前(MMDの標準の名前と違うモデルだけ書く)
	-- 	-- bones = { center = "センター", upper = "上半身", lower = "下半身", rightLeg = "右足", ... },
	-- },
}
