-- フィールドエディタの画面: 下にタイルのパレット(クリックか数字キーで選ぶ)、左上に情報、右上に操作説明。Escでメニュー(保存・読み直し・全面を塗る・終了)。
-- C++(FieldEditorScene)から world.brush / cellX / cellZ / dirty / saved / width / depth / fps が渡される。命令は game.command(名前, 値)

loadScript("res/lua/data/field_tiles.lua") -- tiles (ゲームと同じ定義)

local swatch = 96
local gap = 18
local swatches = {}
local brush = -1
local brushText, cellText, stateText, toast
local toastTime = 0
local lastSaved = world.saved or 0
local shown = {}

-- Escのメニュー(保存・読み直しなど、うっかり押したくない操作は、ここから)
local menu, menuItems = nil, {}
local menuSelected = 1
local menuOpen = false
local menuDefs = {
	{key = "EditMenuResume", action = function() end},
	{key = "EditMenuSave", action = function() game.command("save") end},
	{key = "EditMenuReload", action = function() game.command("load") end},
	{key = "EditMenuFill", action = function() game.command("fill") end},
	{key = "EditMenuQuit", action = function() scene.quit() end},
}

local function selectMenu(index)
	menuSelected = (index - 1) % #menuDefs + 1
	for i, item in ipairs(menuItems) do
		local current = i == menuSelected
		item:setColor(current and 255 or 210, current and 220 or 210, current and 90 or 210)
		item:animate("scale", current and 1.12 or 1, 0.1, "decelerate")
	end
end

local function setMenu(open)
	menuOpen = open
	menu:setVisible(open)
	if open then
		selectMenu(1)
	end
end

local function runMenu(index)
	setMenu(false)
	menuDefs[index].action()
end

local function select(index)
	game.command("brush", index)
end

local function showToast(key)
	toast:setTextKey(key)
	toast:setAlpha(1)
	toast:animate("alpha", 0, 1.2, "accelerate", {delay = 0.8})
end

function init()
	-- 下: パレット
	local panelWidth = #tiles * (swatch + gap) + gap
	local panel = ui.rect()
	panel:setSize(panelWidth, swatch + gap * 2 + 40)
	panel:setAnchor(0.5, 1)
	panel:setPivot(0.5, 1)
	panel:setPos(0, -24)
	panel:setColor(0, 0, 0, 150)
	panel:setInteractive(true) -- パレットの上では、地面を塗らない
	root:add(panel)
	for i, tile in ipairs(tiles) do
		local box = ui.rect()
		box:setSize(swatch, swatch)
		box:setPos(gap + (i - 1) * (swatch + gap) + swatch / 2, gap + swatch / 2)
		box:setPivot(0.5, 0.5)
		box:setColor(tile.color[1], tile.color[2], tile.color[3], 255)
		box:onEnter(function(w) w:animate("scale", 1.12, 0.12, "decelerate") end)
		box:onLeave(function(w) w:animate("scale", 1, 0.12, "decelerate") end)
		box:onClick(function() select(i - 1) end)
		panel:add(box)
		local number = ui.text(tostring(i), 32)
		number:setColor(255, 255, 255)
		number:setPos(8, 4)
		box:add(number)
		local label = ui.text(nil, {size = 22, key = tile.name})
		label:setColor(230, 230, 230)
		label:setPos(gap + (i - 1) * (swatch + gap), gap + swatch + 8)
		panel:add(label)
		swatches[i] = box
	end

	-- 左上: 情報
	local info = ui.rect()
	info:setSize(420, 150)
	info:setPos(24, 24)
	info:setColor(0, 0, 0, 130)
	root:add(info)
	brushText = ui.text("", 32)
	brushText:setPos(14, 8)
	info:add(brushText)
	cellText = ui.text("", {size = 28, bitmap = true}) -- 毎フレーム変わる数字は、ビットマップフォント
	cellText:setPos(14, 54)
	info:add(cellText)
	stateText = ui.text("", {size = 28, bitmap = true})
	stateText:setPos(14, 98)
	info:add(stateText)

	-- 右上: 操作説明
	local help = ui.rect()
	help:setSize(640, 250)
	help:setAnchor(1, 0)
	help:setPivot(1, 0)
	help:setPos(-24, 24)
	help:setColor(0, 0, 0, 130)
	root:add(help)
	local keys = {"EditHelpPaint", "EditHelpCamera", "EditHelpMove", "EditHelpBrush", "EditHelpSave", "EditHelpUndo"}
	for i, key in ipairs(keys) do
		local t = ui.text(nil, {size = 26, key = key})
		t:setColor(235, 235, 235)
		t:setPos(14, 10 + (i - 1) * 38)
		help:add(t)
	end

	-- メニュー(Esc)。全面を覆う透明な板が、マウスを受けて、後ろの地面を塗らせない
	menu = ui.group()
	local shade = ui.rect()
	shade:setColor(0, 0, 0, 150)
	shade:setInteractive(true)
	menu:add(shade)
	local title = ui.text(nil, {size = 64, key = "EditMenuTitle"})
	title:setAnchor(0.5, 0.5)
	title:setPivot(0.5, 0.5)
	title:setPos(0, -250)
	menu:add(title)
	for i, def in ipairs(menuDefs) do
		local item = ui.text(nil, {size = 48, key = def.key})
		item:setAnchor(0.5, 0.5)
		item:setPivot(0.5, 0.5)
		item:setPos(0, -130 + (i - 1) * 80)
		item:onEnter(function() selectMenu(i) end)
		item:onClick(function() runMenu(i) end)
		menu:add(item)
		menuItems[i] = item
	end
	menu:setVisible(false)
	root:add(menu)

	-- 中央: 保存したなどの短い通知
	toast = ui.text(nil, {size = 56, key = "EditSaved"})
	toast:setAnchor(0.5, 0.35)
	toast:setPivot(0.5, 0.5)
	toast:setColor(255, 240, 120)
	toast:setAlpha(0)
	root:add(toast)
end

function update(dt, time)
	local current = math.floor(world.brush or 0)
	if current ~= brush then
		brush = current
		for i, box in ipairs(swatches) do
			if i - 1 == brush then
				box:setBorder(6, 255, 255, 255)
			else
				box:setBorder(0, 0, 0, 0)
			end
		end
		brushText:setText("[" .. (brush + 1) .. "] " .. (i18n.t(tiles[brush + 1].name)))
	end
	cellText:setText(world.cellX >= 0 and string.format("%d, %d", world.cellX, world.cellZ) or "-")
	stateText:setText(string.format("%dx%d  %s  FPS %d", world.width, world.depth, world.dirty > 0 and "*" or "", math.floor(world.fps + 0.5)))
	if world.saved ~= lastSaved then
		lastSaved = world.saved
		showToast("EditSaved")
	end
end

function onKey(key, down)
	if not down then
		return
	end
	if key == "Escape" then
		setMenu(not menuOpen)
		return
	end
	if menuOpen then
		if key == "Up" then
			selectMenu(menuSelected - 1)
		elseif key == "Down" then
			selectMenu(menuSelected + 1)
		elseif key == "Return" or key == "Space" then
			runMenu(menuSelected)
		end
		return -- メニューを開いている間は、他のキー操作(タイルの選択など)は受けない
	end
	local number = tonumber(key)
	if number and number >= 1 and number <= #tiles then
		select(number - 1)
	elseif key == "Z" then
		game.command("undo")
	end
end
