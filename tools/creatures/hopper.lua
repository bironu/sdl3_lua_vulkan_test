-- 丸い4本足の敵「hopper」の定義(ツール creature2glb が読む。ゲームは読まない)。長さはメートル(形の長さの単位は任意)、時間は秒、角度はラジアン。
-- 書き出し: creature2glb tools/creatures/hopper.lua res/model/enemy/hopper.glb
-- 無い項目は、C++側の既定値(tools/creature/CreatureBuilder.h の CreatureSpec、tools/creature/CreatureAnimator.h の CreatureMotion)のまま。
-- 書き出したときにログに出る、歩きの stride と、攻撃の区間の秒を、res/lua/data/enemies.lua の motions に書くこと
creature = {
	name = "hopper",       -- メッシュ・スキンの名前
	length = 1.1,          -- 体長(m)。形の前後の長さを、これに合わせて拡大縮小して書き出す
	-- 体の形: 丸い卵形の胴体の前面に顔、細い棒の手足。向きの言葉: 前は体の正面、外は左右の外側、上は上。
	-- 胴体: 卵形(taper で前を細く・後ろを太く)。長さ・幅・高さは、いちばん太い所の直径
	body = { length = 0.95, width = 0.86, height = 0.84, taper = 0.05, slices = 22, sides = 22 },
	-- 顔(胴体の前面に、表面に沿って lift だけ浮かせて貼る): pitch は顔の中心の向き(正面から上へ)。位置は顔の中心からの 外・上 のずれ。
	-- 閉じ目は上に凸の弧(eyeRadius の円の eyeSpan の角度分。太さ eyeThickness)、口は縦長の楕円の板
	face = {
		pitch = -0.1, lift = 0.004,
		eyeSpacing = 0.12, eyeUp = 0.05, eyeRadius = 0.05, eyeSpan = 2.1, eyeThickness = 0.012, eyeSegments = 10,
		mouthUp = -0.09, mouthWidth = 0.05, mouthHeight = 0.075, mouthSides = 16,
	},
	-- 手足の筒: 周の分割、区間ごとのリングの数、関節を丸めて重みを混ぜる幅(区間の長さに対する割合)、付け根を胴体へ入れる長さ(太さの倍数)
	legMesh = { sides = 8, rings = 6, blend = 0.3, inset = 1.5 },
	-- 手足(1対ずつ、左右対称)。name: ボーンの名前の頭(<name>_<left|right>_hip/knee/ankle/foot)。along: 付け根の前後の位置(後ろの端 -1〜前の端 +1)、
	-- angle: 周の位置(真横 0、下が負)。knee・ankle・foot: 1つ前の関節からのずれ {外, 上, 前}。radius: 太さ(付け根・ひざ・足首・足先)。
	-- いちばん低い足先が地面(Y=0)に着く。動きは、付け根の前後の位置で、いちばん前の1対を前足、いちばん後ろの1対を後ろ足として作る
	legs = {
		-- 前の1対: 胴体の前の下から、ひざが外・前へ出て、すねが下へ降りる
		{ name = "front", along = 0.45, angle = -0.6,
			knee = { 0.15, 0.05, 0.08 }, ankle = { 0.03, -0.3, 0.02 }, foot = { 0.0, -0.04, 0.05 },
			radius = { 0.034, 0.03, 0.027, 0.022 } },
		-- 後ろの1対: 胴体の後ろの下から、ひざが外・後ろへ出て、すねが下へ降りる
		{ name = "back", along = -0.5, angle = -0.6,
			knee = { 0.14, 0.05, -0.1 }, ankle = { 0.03, -0.3, -0.02 }, foot = { 0.0, -0.04, 0.03 },
			radius = { 0.034, 0.03, 0.027, 0.022 } },
	},
	-- 色(rgb 0〜1): 胴体の背側・腹側、手足、顔(目・口)
	color = {
		back = { 0.95, 0.92, 0.82 }, belly = { 0.98, 0.95, 0.87 }, legs = { 0.80, 0.76, 0.68 },
		face = { 0.08, 0.07, 0.07 },
	},
	bellyLine = -1.0, -- 背と腹の境目(面の向きの上下の成分。これより上が背の色。-1で全部、背の色。境目は三角形の単位でぎざぎざになるので、色を分けるなら近い色に)

	-- 以下は動き(クリップに焼き込む。その場の姿勢だけ。潰れは root ボーンの拡大縮小)。
	-- 歩き(2通り)。対角の足が同時に出る。角度は付け根・ひざの回転。クリップの長さは stride ÷ speed(1周期)。
	-- speed: 進む速さ(ゲームの速さは enemies.lua の behavior。合わせること)、stride: 1周期(全部の足が1歩ずつ)で進む距離、duty: 足が接地している割合、
	-- swing: 足を前後に振る角度、lift・kneeLift: 足を前へ戻す間に、付け根で持ち上げる・ひざを曲げる角度、
	-- bob: 胴体の上下の弾み(m)、hop: 弾みの形(0で滑らかな波、1で地面で跳ね返る形)、nod: 1歩ごとに胴体を前後に傾ける角度、sway・roll: 左右の揺れ(m)・傾き
	walkFast = { speed = 1.8, stride = 0.55, duty = 0.55, swing = 0.4, lift = 0.25, kneeLift = 0.45, bob = 0.012, hop = 0.3, nod = 0.02, sway = 0.008, roll = 0.03 },
	walkSlow = { speed = 0.8, stride = 0.6, duty = 0.6, swing = 0.35, lift = 0.3, kneeLift = 0.5, bob = 0.035, hop = 1.0, nod = 0.05, sway = 0.012, roll = 0.06 },
	-- 待機: 呼吸の周期、胴体の上下(m)・前後の傾き、前足を振る角度
	idle = { period = 2.8, breath = 0.012, pitch = 0.03, frontSwing = 0.12 },
	-- 攻撃A(プレイヤーの近く): 後ろ足の付け根を支点に起き上がり → 前足をわきわき → 前へ倒れ込む(顔から地面へ) → 止まる → 戻る
	attackStand = {
		rise = 0.5,          -- 起き上がる時間
		angle = 0.785,       -- 起き上がる角度(胴体の傾き。45度)
		frontRaise = 0.4,    -- 起き上がったときに、前足を付け根で前へ上げる角度
		frontFold = 0.6,     -- 同、ひざを曲げる角度
		wiggle = 1.0,        -- わきわきの時間
		wiggleCount = 4,     -- わきわきの回数
		wiggleSwing = 0.5,   -- わきわきで、前足を付け根で振る角度(左右は逆向き)
		wiggleKnee = 0.6,    -- 同、ひざを曲げ伸ばしする角度
		fall = 0.4,          -- 倒れ込む時間
		fallEase = 2.5,      -- 倒れ込みの加速(ease-in の指数。大きいほど最後に勢いがつく)
		fallAngle = -0.35,   -- 倒れたときの胴体の傾き(負で前が下がる)
		reach = 1.0,         -- 倒れ込みで、前足を付け根で前へ伸ばす角度
		reachOpen = 0.8,     -- 同、外へ開く角度
		reachKnee = -0.3,    -- 同、ひざの角度(負で伸ばす)
		bounce = 0.08,       -- 倒れた直後に、胴体が跳ね返る角度
		hold = 0.6,          -- 倒れたまま止まる時間
		recover = 0.7,       -- 起き上がって戻る時間
		-- 顔から地面に着いた瞬間の「びたーん」: 高さを amount の割合だけ縮め(前後・左右へ stretch × その割合だけ広げ)、減衰するバネのように
		-- duration で戻る。bounce は跳ね返り(戻ったあと逆に縦へ伸びる量の、潰れに対する割合)
		squash = { amount = 0.22, stretch = 0.5, duration = 0.45, bounce = 0.3 },
	},
	-- 攻撃B(少し離れた所から): 溜め(体を沈める) → 前へ跳ぶ → 足を広げて、お腹から着地 → 止まる → 戻る。
	-- その場の姿勢だけ(跳ぶ軌道は、ゲームが enemies.lua の jump の値で決め、空中の区間 air を、滞空時間に合わせて伸縮して再生する)
	attackJump = {
		crouch = 0.45,       -- 溜めの時間
		crouchDepth = 0.1,   -- 溜めで、胴体を沈める深さ(m)
		crouchKnee = 0.35,   -- 溜めで、ひざを曲げる角度
		crouchPitch = -0.08, -- 溜めの胴体の傾き(負で前が下がる)
		launch = 0.1,        -- 伸び上がる(地面を蹴る)時間
		air = 0.6,           -- 空中の姿勢の長さ(クリップの時刻)
		airPitch = 0.25,     -- 跳び上がったときの胴体の傾き(着地までに0へ戻る)
		spread = 1.15,       -- 着地で、足を付け根で外へ持ち上げる角度(手足を投げ出す)
		spreadSwing = 0.45,  -- 同、前足を前へ・後ろ足を後ろへ振る角度
		bounce = 0.06,       -- 着地の直後に、胴体が揺れる角度
		hold = 0.6,          -- 着地したまま止まる時間
		recover = 0.8,       -- 起き上がって戻る時間
		airStretch = 0.08,   -- 跳んでいる間に、縦へ伸びる割合(跳び立ち・着地の前ほど大きく)
		squash = { amount = 0.28, stretch = 0.5, duration = 0.45, bounce = 0.3 }, -- 着地の瞬間の潰れ(attackStand.squash と同じ)
	},
	-- 倒れ(いまは使う場面が無い): 手足を縮めて横へ倒れる
	death = { duration = 0.8, roll = 1.5, curl = 1.1 },
}
