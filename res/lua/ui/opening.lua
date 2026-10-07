-- OpeningScene: 画面の真ん中に白い文字 "Push Enter" を0.5秒間隔で点滅。Enterでキャラクタ選択へ。
-- F5でこのスクリプトを読み直せる(アプリの再ビルドは不要)。座標は論理画面(screen.width x screen.height)、左上原点

local blinkInterval = 0.5
local text
local languageLabel
local nextLanguage -- 下で定義(initのコールバックから呼ぶため、先に宣言)

function init()
	local background = ui.background()
	background:setImage("res/image/OpeningBack.jpeg") -- 画面いっぱいに伸ばして表示する
	root:add(background)

	text = ui.text(nil, {size = 96, key = "PushEnter"}) -- 文字列は、言語ファイル(res/lua/lang/*.lua)のキーで指定する
	text:setColor(255, 255, 255)
	text:setOutline(5, 0, 0, 0) -- 黒い縁取り(明るい背景でも読めるように)
	text:setAnchor(0.5, 0.5) -- 親(画面)の真ん中に
	text:setPivot(0.5, 0.5) -- 自分の真ん中を合わせる
	root:add(text)

	-- 右上に、現在の言語の名前(Lキーで、選べる言語を順に切り替える。文字列は自動で更新される)
	languageLabel = ui.text(nil, {size = 32, key = "LanguageName"})
	languageLabel:setColor(160, 160, 160)
	languageLabel:setAnchor(1, 0)
	languageLabel:setPivot(1, 0)
	languageLabel:setPos(-40, 30)
	root:add(languageLabel)
	-- マウスが乗ったらふわふわ(拡大縮小のループ)、出たら元へ。クリックで言語を切り替える。補間は "bounce" "overshoot" "cycle" なども選べる
	languageLabel:setPivot(1, 0)
	languageLabel:onEnter(function(w)
		w:animate("scale", 1.2, 0.5, "accelerateDecelerate", {loop = "pingpong"})
		w:setColor(255, 255, 255)
	end)
	languageLabel:onLeave(function(w)
		w:stopAnimation("scale")
		w:animate("scale", 1, 0.15, "decelerate")
		w:setColor(160, 160, 160)
	end)
	languageLabel:onClick(function(w, button)
		nextLanguage()
	end)
end

nextLanguage = function()
	local languages = i18n.languages()
	for i, name in ipairs(languages) do
		if name == i18n.language() then
			i18n.setLanguage(languages[i % #languages + 1])
			return
		end
	end
end

function update(dt, time)
	text:setVisible(math.floor(time / blinkInterval) % 2 == 0)
end

function onKey(key, down)
	if not down then
		return
	end
	if key == "Return" or key == "Keypad Enter" or key == "PadA" or key == "PadStart" then
		scene.change("characterselect")
	elseif key == "L" then
		nextLanguage()
	elseif key == "Escape" then
		scene.quit()
	end
end
