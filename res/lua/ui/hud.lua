-- GameScene の HUD: 左上にFPSと位置、左下に操作説明(Hで表示切替)、右下にミニマップ(Mで表示切替)。
-- ゲームの状態は world.x / world.z / world.fps などで読める(C++が毎フレーム更新する)。

local margin = 24
local mapSize = 280
local helpKeys = {"HudHelpTitle", "HudHelpMove", "HudHelpRun", "HudHelpRoll", "HudHelpAttack", "HudHelpCamera", "HudHelpZoom", "HudHelpMap", "HudHelpQuit"}
local textSize = 28
local lineHeight = 36

local fpsText, posText, help, minimap
local refresh = 0 -- FPSの数字を更新する間隔

local function label(key, size, x, y, anchorX, anchorY, parent, bitmap)
	local t = ui.text(nil, {size = size, key = key, bitmap = bitmap})
	t:setColor(255, 255, 255)
	t:setAnchor(anchorX, anchorY)
	t:setPivot(anchorX, anchorY)
	t:setPos(x, y)
	parent:add(t)
	return t
end

function init()
	-- 左上: FPS と位置
	local top = ui.rect()
	top:setSize(300, 84)
	top:setPos(margin, margin)
	top:setColor(0, 0, 0, 110)
	root:add(top)
	fpsText = label("HudFps", 32, 14, 8, 0, 0, top, true) -- 数字が変わる文字は、ビットマップフォント(ASCIIだけ。文字を作り直さない)
	posText = label("HudPosition", textSize, 14, 46, 0, 0, top, true)

	-- 左下: 操作説明
	help = ui.rect()
	help:setSize(820, lineHeight * #helpKeys + 20)
	help:setAnchor(0, 1)
	help:setPivot(0, 1)
	help:setPos(margin, -margin)
	help:setColor(0, 0, 0, 110)
	root:add(help)
	for i, key in ipairs(helpKeys) do
		local t = label(key, textSize, 14, 10 + (i - 1) * lineHeight, 0, 0, help)
		if i == 1 then
			t:setColor(255, 220, 100)
		end
	end

	-- 右下: ミニマップ(メートル単位の範囲は、setRangeで)
	minimap = ui.minimap()
	minimap:setSize(mapSize, mapSize)
	minimap:setAnchor(1, 1)
	minimap:setPivot(1, 1)
	minimap:setPos(-margin, -margin)
	minimap:setRange(8)
	root:add(minimap)
end

function update(dt, time)
	posText:setTextVars({x = string.format("%.1f", world.x), z = string.format("%.1f", world.z)}) -- 毎フレーム更新しても軽い
	refresh = refresh - dt
	if refresh <= 0 then
		refresh = 0.25 -- FPSは、読めるよう、間隔をあけて更新する
		fpsText:setTextVars({fps = math.floor(world.fps + 0.5)})
	end
end

function onKey(key, down)
	if not down then
		return
	end
	if key == "H" then
		help:animate("alpha", help:getAlpha() > 0.5 and 0 or 1, 0.15, "easeOut")
	elseif key == "M" then
		minimap:animate("alpha", minimap:getAlpha() > 0.5 and 0 or 1, 0.15, "easeOut")
	end
end
