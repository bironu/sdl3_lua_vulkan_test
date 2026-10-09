-- GameScene が使うデータの読み込み一覧。LoadingScene が別スレッドで先に読み、GameScene の開始時にも(読み込み済みなので速く)同じ一覧で取得する。
-- パスはリポジトリ直下からの相対パス。

-- キャラのモーション(VRMA)の表: 名前 → パス。C++ は、この「名前」でパスを引く(名前は GameScene.cpp の kMotionNames と同じ。
-- 名前を足すときは、C++ の列挙と kMotionNames も足す)。モーションを差し替えるときは、ここのパスだけを変える
motions = {
	idle = "res/motion/Standing Idle.vrma",        -- 立ち
	walk = "res/motion/Walking.vrma",              -- 歩き(左スティックを倒しきらない)
	slowRun = "res/motion/Slow Run.vrma",          -- 左スティックを最大に倒す
	fastRun = "res/motion/Fast Run.vrma",          -- Aボタンを押しっぱなし + 左スティックを最大に倒す
	climb = "res/motion/Climbing Slope.vrma",      -- 歩いて登れない少し急な坂へ進む(ゆっくり登る)
	roll = "res/motion/Stand To Roll.vrma",        -- Aボタン単押し
	punchRight = "res/motion/Punching Right.vrma", -- R1
	punchLeft = "res/motion/Punching Left.vrma",   -- L1
	kickHigh = "res/motion/Mma Kick Right High.vrma", -- R2
	roundhouse = "res/motion/Roundhouse Kick.vrma",   -- L2
}

assets = {
	-- (プレイヤーのモデルは、キャラクタ選択画面で選んだもの。選択画面が読んでおく)
	animations = {},
}
-- 上の表のモーションを、全部読む(表に足せば、読み込み一覧にも入る)
for _, path in pairs(motions) do
	table.insert(assets.animations, path)
end
