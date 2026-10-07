-- GameScene が使うデータの読み込み一覧。LoadingScene が別スレッドで先に読み、GameScene の開始時にも(読み込み済みなので速く)同じ一覧で取得する。
-- パスはリポジトリ直下からの相対パス。コード側(GameScene.cpp の kMotionPaths)が使うパスと同じものを書くこと。
assets = {
	-- (プレイヤーのモデルは、キャラクタ選択画面で選んだもの。選択画面が読んでおく)
	-- キャラのモーション(VRMA): 立ち・歩き・走り(Slow/Fast)・転がる・攻撃(パンチ・キック)
	animations = {
		"res/motion/VRMA_01.vrma",
		"res/motion/Walking.vrma",
		"res/motion/Slow Run.vrma",
		"res/motion/Fast Run.vrma",
		"res/motion/Stand To Roll.vrma",
		"res/motion/Punching Right.vrma",
		"res/motion/Punching Left.vrma",
		"res/motion/Martelo 2.vrma",
		"res/motion/Roundhouse Kick.vrma",
	},
}
