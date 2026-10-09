-- 6本足の敵「crawler」の定義(ツール creature2glb が読む。ゲームは読まない)。項目の意味は tools/creatures/hopper.lua と同じ。
-- 書き出し: creature2glb tools/creatures/crawler.lua res/model/enemy/crawler.glb
-- hopper より小さく、横長の平たい卵形に、3対の短い足。ミントの色。歩きは三脚歩行(前左・中右・後左と、前右・中左・後右が交互に出る)
creature = {
	name = "crawler",
	length = 0.8,          -- 体長(m)
	body = { length = 1.0, width = 0.86, height = 0.56, taper = 0.12, slices = 22, sides = 22 },
	face = {
		pitch = 0.05, lift = 0.004,
		eyeSpacing = 0.15, eyeUp = 0.04, eyeRadius = 0.045, eyeSpan = 2.4, eyeThickness = 0.014, eyeSegments = 10,
		mouthUp = -0.07, mouthWidth = 0.07, mouthHeight = 0.05, mouthSides = 16,
	},
	legMesh = { sides = 8, rings = 6, blend = 0.3, inset = 1.5 },
	-- 3対の足(前・中・後)。短く、外へ大きく張り出す
	legs = {
		{ name = "front", along = 0.5, angle = -0.45,
			knee = { 0.16, 0.06, 0.08 }, ankle = { 0.05, -0.22, 0.03 }, foot = { 0.0, -0.035, 0.04 },
			radius = { 0.04, 0.035, 0.03, 0.024 } },
		{ name = "middle", along = 0.0, angle = -0.45,
			knee = { 0.18, 0.06, 0.0 }, ankle = { 0.05, -0.22, 0.0 }, foot = { 0.0, -0.035, 0.0 },
			radius = { 0.04, 0.035, 0.03, 0.024 } },
		{ name = "back", along = -0.5, angle = -0.45,
			knee = { 0.16, 0.06, -0.08 }, ankle = { 0.05, -0.22, -0.03 }, foot = { 0.0, -0.035, -0.04 },
			radius = { 0.04, 0.035, 0.03, 0.024 } },
	},
	color = {
		back = { 0.55, 0.85, 0.72 }, belly = { 0.68, 0.91, 0.80 }, legs = { 0.36, 0.60, 0.52 },
		face = { 0.10, 0.12, 0.10 },
	},
	bellyLine = -0.2, -- 背(ミント)とお腹(薄い色)を分ける

	-- 動き: ちょこちょこ速く歩く(1周期が短く、振りは小さい)
	walkFast = { speed = 2.2, stride = 0.36, duty = 0.5, swing = 0.38, lift = 0.35, kneeLift = 0.5, bob = 0.01, hop = 0.2, nod = 0.015, sway = 0.006, roll = 0.04 },
	walkSlow = { speed = 1.0, stride = 0.4, duty = 0.55, swing = 0.32, lift = 0.35, kneeLift = 0.55, bob = 0.02, hop = 0.6, nod = 0.03, sway = 0.01, roll = 0.05 },
	idle = { period = 2.0, breath = 0.01, pitch = 0.04, frontSwing = 0.2 },
	attackStand = {
		rise = 0.4, angle = 0.6, frontRaise = 0.5, frontFold = 0.7,
		wiggle = 0.8, wiggleCount = 5, wiggleSwing = 0.55, wiggleKnee = 0.7,
		fall = 0.35, fallEase = 2.5, fallAngle = -0.3,
		reach = 1.1, reachOpen = 0.7, reachKnee = -0.3,
		bounce = 0.1, hold = 0.5, recover = 0.6,
		squash = { amount = 0.3, stretch = 0.5, duration = 0.45, bounce = 0.35 },
	},
	attackJump = {
		crouch = 0.35, crouchDepth = 0.06, crouchKnee = 0.4, crouchPitch = -0.06,
		launch = 0.08, air = 0.5, airPitch = 0.2,
		spread = 1.2, spreadSwing = 0.5, bounce = 0.08,
		hold = 0.5, recover = 0.6, airStretch = 0.1,
		squash = { amount = 0.32, stretch = 0.5, duration = 0.45, bounce = 0.35 },
	},
	death = { duration = 0.7, roll = 1.6, curl = 1.2 },
}
