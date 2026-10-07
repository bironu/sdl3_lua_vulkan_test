-- キャラクタ選択画面(CharacterSelectScene)。3Dで水平に回るキャラは、C++が描く。このスクリプトは、名前・矢印・案内・読み込み中の円を出し、入力を受ける。
--   左右(←→ / 十字キー / 左スティック): 回す  Enter / PadA: 決定(読み込みが終わっていれば)  Backspace / PadB: Openingへ戻る  Esc: 終了
-- C++から world.selected(1始まり) / count / ready(選択中の読み込みが終わったか) / failed が渡される。命令は game.command("rotate", ±1) / ("confirm")

loadScript("res/lua/data/characters.lua") -- characters

local spinnerDots = 12
local spinnerRadius = 46
local spinnerSpeed = 1.1 -- 1秒に回る周数

local nameText, countText, spinner, hint, failedText
local dots = {}
local shownIndex = 0

local cursorSound = "res/sound/cursor7.mp3" -- 回したとき
local decideSound = "res/sound/cursor1.mp3" -- 決定したとき

local function rotate(delta)
	audio.play(cursorSound)
	game.command("rotate", delta)
end

local function confirm()
	if world.ready == 1 then -- 読み込み中は決定できない(音も鳴らさない)
		audio.play(decideSound)
	end
	game.command("confirm")
end

function init()
	-- 名前(下の真ん中)と、何番目か
	nameText = ui.text("", 64)
	nameText:setColor(255, 255, 255)
	nameText:setAnchor(0.5, 1)
	nameText:setPivot(0.5, 1)
	nameText:setPos(0, -150)
	root:add(nameText)

	countText = ui.text("", 32)
	countText:setColor(200, 200, 210)
	countText:setAnchor(0.5, 1)
	countText:setPivot(0.5, 1)
	countText:setPos(0, -96)
	root:add(countText)

	hint = ui.text(nil, {size = 30, key = "CharaHint"})
	hint:setColor(255, 220, 100)
	hint:setAnchor(0.5, 1)
	hint:setPivot(0.5, 1)
	hint:setPos(0, -36)
	root:add(hint)

	-- 左右の矢印(クリックでも回る)
	for _, side in ipairs({-1, 1}) do
		local arrow = ui.text(side < 0 and "<" or ">", 120)
		arrow:setColor(255, 255, 255)
		arrow:setAnchor(side < 0 and 0 or 1, 0.5)
		arrow:setPivot(0.5, 0.5)
		arrow:setPos(side < 0 and 110 or -110, 0)
		arrow:setAlpha(0.6)
		arrow:onEnter(function(w) w:animate("alpha", 1, 0.1, "decelerate"); w:animate("scale", 1.2, 0.1, "decelerate") end)
		arrow:onLeave(function(w) w:animate("alpha", 0.6, 0.1, "decelerate"); w:animate("scale", 1, 0.1, "decelerate") end)
		arrow:onClick(function() rotate(side) end)
		root:add(arrow)
	end

	-- 読み込み中の、ぐるぐる回る円(小さな点が輪になり、明るい点が回る)
	spinner = ui.group()
	spinner:setAnchor(0.5, 0.45)
	root:add(spinner)
	for i = 1, spinnerDots do
		local dot = ui.rect()
		dot:setSize(14, 14)
		dot:setPivot(0.5, 0.5)
		local angle = (i - 1) / spinnerDots * math.pi * 2
		dot:setPos(math.sin(angle) * spinnerRadius, -math.cos(angle) * spinnerRadius)
		dot:setColor(255, 255, 255)
		spinner:add(dot)
		dots[i] = dot
	end

	failedText = ui.text(nil, {size = 40, key = "CharaFailed"})
	failedText:setColor(255, 120, 120)
	failedText:setAnchor(0.5, 0.45)
	failedText:setPivot(0.5, 0.5)
	failedText:setVisible(false)
	root:add(failedText)
end

function update(dt, time)
	local index = math.floor(world.selected or 1)
	if index ~= shownIndex and characters[index] then
		shownIndex = index
		nameText:setText(characters[index].name)
		countText:setText(index .. " / " .. #characters)
		nameText:setAlpha(0)
		nameText:animate("alpha", 1, 0.25, "decelerate")
	end
	local loading = world.ready == 0 and world.failed == 0
	spinner:setVisible(loading)
	failedText:setVisible(world.failed == 1)
	hint:setVisible(world.ready == 1)
	if loading then
		-- 先頭の点が最も明るく、後ろへ尾を引いて暗くなる
		local head = (time * spinnerSpeed * spinnerDots) % spinnerDots
		for i, dot in ipairs(dots) do
			local behind = (head - (i - 1)) % spinnerDots
			dot:setAlpha(math.max(0.15, 1 - behind / spinnerDots * 1.1))
		end
	end
end

function onKey(key, down)
	if not down then
		return
	end
	if key == "Left" or key == "PadLeft" then
		rotate(-1)
	elseif key == "Right" or key == "PadRight" then
		rotate(1)
	elseif key == "Return" or key == "Keypad Enter" or key == "PadA" then
		confirm()
	elseif key == "Backspace" or key == "PadB" then
		scene.change("opening")
	elseif key == "Escape" then
		scene.quit()
	end
end
