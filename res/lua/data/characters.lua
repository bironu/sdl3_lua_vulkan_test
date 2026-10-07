-- 選べるキャラクタ(キャラクタ選択画面に、この順で、水平に並ぶ。GameSceneは選んだもので動く)。res/model/ のモデルを足すには、ここへ1行足す。
-- name: 表示名、model: モデル(VRM)のパス、motion: 選択画面の立ち姿のモーション(VRMA。省略すると res/motion/VRMA_01.vrma)
characters = {
	{name = "テストモデル1", model = "res/model/sdl3_lua5_vulkan_test1.vrm", motion = "res/motion/VRMA_01.vrma"},
	{name = "テストモデル2", model = "res/model/sdl3_lua5_vlukan_test2.vrm", motion = "res/motion/VRMA_02.vrma"},
	{name = "テストモデル3", model = "res/model/sdl3_lua5_vulkan_test3.vrm", motion = "res/motion/VRMA_03.vrma"},
	{name = "金髪の女の子", model = "res/model/1990430679655844163.vrm", motion = "res/motion/VRMA_04.vrma"},
}
