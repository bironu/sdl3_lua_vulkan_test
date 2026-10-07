-- GameScene が使うデータの読み込み一覧。LoadingScene が別スレッドで先に読み、GameScene の開始時にも(読み込み済みなので速く)同じ一覧で取得する。
-- パスはリポジトリ直下からの相対パス。コード側(GameScene.cpp)が使うパスと同じものを書くこと。
assets = {
	-- (プレイヤーのモデルは、キャラクタ選択画面で選んだもの。選択画面が読んでおく)
	-- 歩きと、立ち止まっているときのモーション(VRMA)
	animations = {"res/motion/Walking.vrma", "res/motion/VRMA_01.vrma"},
}
