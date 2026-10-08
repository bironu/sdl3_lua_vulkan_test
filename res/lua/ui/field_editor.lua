-- フィールドエディタの画面: 上にモード(タイル/高さ/置物)、下にモードごとのパレット(タイル・高さの道具・置物の一覧。クリックか数字キーで選ぶ)、
-- 左上に情報、右上に操作説明。Escでメニュー(保存・読み直し・全面を塗る・終了)。
-- C++(FieldEditorScene)から world.mode / brush / tool / radius / propSel / propPage / propPages / propYaw / propScale / propLift / propCount /
-- cellX / cellZ / hoverHeight / slopes / rampSlope / dirty / saved / imported / width / depth / fps と、world.strings.prop1〜prop10(ページの置物の名前)が渡される。命令は game.command(名前, 値)

loadScript("res/lua/data/field_tiles.lua") -- tiles (ゲームと同じ定義)

local swatch = 96
local gap = 18
local swatches = {}
local toolBoxes = {}
local propBoxes, propTexts = {}, {}
local modeButtons = {}
local modeKeys = {"ModeTile", "ModeHeight", "ModeProp"}
local toolKeys = {"ToolRaise", "ToolLower", "ToolSmooth", "ToolFlatten", "ToolRamp"}
local helpKeys = {
	{"EditHelpPaint", "EditHelpRadius"},
	{"EditHelpSculpt", "EditHelpRadius"},
	{"EditHelpPlace", "EditHelpPlace2"},
}
local commonHelpKeys = {"EditHelpCamera", "EditHelpMode", "EditHelpSave", "EditHelpUndo"}
local propSlots = 10
local tilePanel, toolPanel, propPanel
local helpTexts = {}
local pageText
local brushText, cellText, stateText, detailText, toast, legend
local lastSaved = world.saved or 0
local lastImported = world.imported or 0
local shown = {mode = -1, brush = -1, tool = -1, propSel = -99, propPage = -1}
local shownProp = {}

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

local function setMode(mode)
	game.command("mode", mode)
end

local function showToast(key)
	toast:setTextKey(key)
	toast:setAlpha(1)
	toast:animate("alpha", 0, 1.2, "accelerate", {delay = 0.8})
end

local function panel(width, height)
	local p = ui.rect()
	p:setSize(width, height)
	p:setAnchor(0.5, 1)
	p:setPivot(0.5, 1)
	p:setPos(0, -24)
	p:setColor(0, 0, 0, 150)
	p:setInteractive(true) -- パレットの上では、地面を塗らない
	root:add(p)
	return p
end

function init()
	-- 上: モード
	local barWidth = #modeKeys * 260 + gap
	local bar = ui.rect()
	bar:setSize(barWidth, 72)
	bar:setPos(24, 224) -- 左の列(情報の下)
	bar:setColor(0, 0, 0, 150)
	bar:setInteractive(true)
	root:add(bar)
	for i, key in ipairs(modeKeys) do
		local button = ui.rect()
		button:setSize(240, 52)
		button:setPos(gap / 2 + (i - 1) * 260 + 130, 36)
		button:setPivot(0.5, 0.5)
		button:setColor(60, 64, 76, 255)
		button:onClick(function() setMode(i - 1) end)
		bar:add(button)
		local label = ui.text(nil, {size = 30, key = key})
		label:setAnchor(0.5, 0.5)
		label:setPivot(0.5, 0.5)
		button:add(label)
		modeButtons[i] = button
	end

	-- 下: タイルのパレット
	tilePanel = panel(#tiles * (swatch + gap) + gap, swatch + gap * 2 + 40)
	for i, tile in ipairs(tiles) do
		local box = ui.rect()
		box:setSize(swatch, swatch)
		box:setPos(gap + (i - 1) * (swatch + gap) + swatch / 2, gap + swatch / 2)
		box:setPivot(0.5, 0.5)
		box:setColor(tile.color[1], tile.color[2], tile.color[3], 255)
		box:onEnter(function(w) w:animate("scale", 1.12, 0.12, "decelerate") end)
		box:onLeave(function(w) w:animate("scale", 1, 0.12, "decelerate") end)
		box:onClick(function() game.command("brush", i - 1) end)
		tilePanel:add(box)
		local number = ui.text(tostring(i), 32)
		number:setColor(255, 255, 255)
		number:setPos(8, 4)
		box:add(number)
		local label = ui.text(nil, {size = 22, key = tile.name})
		label:setColor(230, 230, 230)
		label:setPos(gap + (i - 1) * (swatch + gap), gap + swatch + 8)
		tilePanel:add(label)
		swatches[i] = box
	end

	-- 下: 高さの道具
	local toolWidth = 240
	toolPanel = panel(#toolKeys * (toolWidth + gap) + gap, 100 + gap)
	for i, key in ipairs(toolKeys) do
		local box = ui.rect()
		box:setSize(toolWidth, 80)
		box:setPos(gap + (i - 1) * (toolWidth + gap) + toolWidth / 2, gap + 40)
		box:setPivot(0.5, 0.5)
		box:setColor(60, 64, 76, 255)
		box:onClick(function() game.command("tool", i - 1) end)
		toolPanel:add(box)
		local label = ui.text(nil, {size = 30, key = key})
		label:setAnchor(0.5, 0.5)
		label:setPivot(0.5, 0.5)
		box:add(label)
		local number = ui.text(tostring(i), 24)
		number:setColor(255, 255, 255)
		number:setPos(8, 4)
		box:add(number)
		toolBoxes[i] = box
	end

	-- 下: 置物の一覧(2行x5列。ページ送りは PageUp / PageDown)
	local slotWidth, slotHeight = 340, 60
	propPanel = panel(5 * (slotWidth + gap) + gap, 2 * (slotHeight + gap) + gap + 40)
	for i = 1, propSlots do
		local col, row = (i - 1) % 5, math.floor((i - 1) / 5)
		local box = ui.rect()
		box:setSize(slotWidth, slotHeight)
		box:setPos(gap + col * (slotWidth + gap) + slotWidth / 2, gap + row * (slotHeight + gap) + slotHeight / 2)
		box:setPivot(0.5, 0.5)
		box:setColor(60, 64, 76, 255)
		box:onClick(function() game.command("prop", i - 1) end)
		propPanel:add(box)
		local number = ui.text(tostring(i % 10), 24)
		number:setColor(200, 200, 200)
		number:setPos(8, 4)
		box:add(number)
		local label = ui.text("", 28)
		label:setPos(44, 12)
		box:add(label)
		propBoxes[i], propTexts[i] = box, label
	end
	pageText = ui.text("", {size = 26, bitmap = true})
	pageText:setAnchor(0.5, 1)
	pageText:setPivot(0.5, 1)
	pageText:setPos(0, -8)
	propPanel:add(pageText)

	-- 左上: 情報
	local info = ui.rect()
	info:setSize(520, 190)
	info:setPos(24, 24)
	info:setColor(0, 0, 0, 130)
	root:add(info)
	brushText = ui.text("", 32)
	brushText:setPos(14, 8)
	info:add(brushText)
	detailText = ui.text("", {size = 28, bitmap = true})
	detailText:setPos(14, 54)
	info:add(detailText)
	cellText = ui.text("", {size = 28, bitmap = true}) -- 毎フレーム変わる数字は、ビットマップフォント
	cellText:setPos(14, 98)
	info:add(cellText)
	stateText = ui.text("", {size = 28, bitmap = true})
	stateText:setPos(14, 142)
	info:add(stateText)

	-- 勾配の凡例(Vキーで勾配を表示しているときだけ)
	legend = ui.rect()
	legend:setSize(340, 150)
	legend:setPos(24, 310)
	legend:setColor(0, 0, 0, 130)
	legend:setVisible(false)
	root:add(legend)
	local legendItems = {
		{key = "EditSlopeWalk", color = {51, 230, 77}},
		{key = "EditSlopeClimb", color = {255, 217, 26}},
		{key = "EditSlopeBlocked", color = {255, 51, 51}},
		{key = "EditSlopeTile", color = {166, 64, 242}},
	}
	for i, item in ipairs(legendItems) do
		local box = ui.rect()
		box:setSize(26, 26)
		box:setPos(14 + 13, 12 + (i - 1) * 34 + 13)
		box:setPivot(0.5, 0.5)
		box:setColor(item.color[1], item.color[2], item.color[3], 255)
		legend:add(box)
		local label = ui.text(nil, {size = 24, key = item.key})
		label:setColor(235, 235, 235)
		label:setPos(54, 10 + (i - 1) * 34)
		legend:add(label)
	end

	-- 右上: 操作説明(モードごとの2行 + 共通)
	local help = ui.rect()
	help:setSize(760, 10 + (#commonHelpKeys + 2) * 38 + 10)
	help:setAnchor(1, 0)
	help:setPivot(1, 0)
	help:setPos(-24, 24)
	help:setColor(0, 0, 0, 130)
	root:add(help)
	for i = 1, #commonHelpKeys + 2 do
		local t = ui.text(nil, {size = 26, key = "EditHelpCamera"})
		t:setColor(235, 235, 235)
		t:setPos(14, 10 + (i - 1) * 38)
		help:add(t)
		helpTexts[i] = t
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

local function refreshMode(mode)
	tilePanel:setVisible(mode == 0)
	toolPanel:setVisible(mode == 1)
	propPanel:setVisible(mode == 2)
	for i, button in ipairs(modeButtons) do
		button:setColor(i - 1 == mode and 200 or 60, i - 1 == mode and 140 or 64, i - 1 == mode and 50 or 76, 255)
	end
	local lines = helpKeys[mode + 1]
	for i, key in ipairs(lines) do
		helpTexts[i]:setTextKey(key)
	end
	for i, key in ipairs(commonHelpKeys) do
		helpTexts[#lines + i]:setTextKey(key)
	end
end

function update(dt, time)
	local mode = math.floor(world.mode or 0)
	local brush = math.floor(world.brush or 0)
	local tool = math.floor(world.tool or 0)
	local propSel = math.floor(world.propSel or -1)
	local propPage = math.floor(world.propPage or 0)
	if mode ~= shown.mode then
		shown.mode = mode
		refreshMode(mode)
		shown.brush, shown.tool, shown.propSel = -1, -1, -99 -- 見出しを作り直す
	end
	if brush ~= shown.brush then
		shown.brush = brush
		for i, box in ipairs(swatches) do
			if i - 1 == brush then
				box:setBorder(6, 255, 255, 255)
			else
				box:setBorder(0, 0, 0, 0)
			end
		end
	end
	if tool ~= shown.tool then
		shown.tool = tool
		for i, box in ipairs(toolBoxes) do
			box:setColor(i - 1 == tool and 200 or 60, i - 1 == tool and 140 or 64, i - 1 == tool and 50 or 76, 255)
		end
	end
	for i = 1, propSlots do
		local name = world.strings["prop" .. i] or ""
		if shownProp[i] ~= name then
			shownProp[i] = name
			propTexts[i]:setText(name)
			propBoxes[i]:setVisible(name ~= "")
		end
		if i - 1 == propSel then
			propBoxes[i]:setBorder(5, 255, 255, 255)
		else
			propBoxes[i]:setBorder(0, 0, 0, 0)
		end
	end
	pageText:setText(string.format("%d / %d", propPage + 1, math.floor(world.propPages or 1)))
	if mode == 0 then
		brushText:setText("[" .. (brush + 1) .. "] " .. i18n.t(tiles[brush + 1].name))
		detailText:setText(string.format("R %d", world.radius))
	elseif mode == 1 then
		brushText:setText("[" .. (tool + 1) .. "] " .. i18n.t(toolKeys[tool + 1]))
		if tool == 4 and math.abs(world.rampSlope) > 0 then
			detailText:setText(string.format("R %d  Slope %.2f", world.radius, math.abs(world.rampSlope)))
		else
			detailText:setText(string.format("R %d", world.radius))
		end
	else
		local name = world.strings["prop" .. (propSel + 1)] or ""
		brushText:setText(name ~= "" and name or "-")
		detailText:setText(string.format("Rot %d  x%.2f  Lift %.2f", math.floor(world.propYaw + 0.5), world.propScale, world.propLift))
	end
	if world.cellX >= 0 then
		cellText:setText(string.format("%d, %d  H %.2f", world.cellX, world.cellZ, world.hoverHeight))
	else
		cellText:setText("-")
	end
	stateText:setText(string.format("%dx%d  Obj %d  %s  FPS %d", world.width, world.depth, world.propCount, world.dirty > 0 and "*" or "", math.floor(world.fps + 0.5)))
	legend:setVisible((world.slopes or 0) > 0)
	if world.saved ~= lastSaved then
		lastSaved = world.saved
		showToast("EditSaved")
	end
	if world.imported ~= lastImported then
		lastImported = world.imported
		showToast("EditImported")
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
	local mode = math.floor(world.mode or 0)
	if key == "T" then
		setMode(0)
	elseif key == "H" then
		setMode(1)
	elseif key == "O" then
		setMode(2)
	elseif key == "C" then
		game.command("collision")
	elseif key == "V" then
		game.command("slopes")
	elseif key == "Z" then
		game.command("undo")
	elseif key == "[" then
		game.command("radius", world.radius - 1)
	elseif key == "]" then
		game.command("radius", world.radius + 1)
	elseif mode == 2 and key == "," then
		game.command("yaw", -15)
	elseif mode == 2 and key == "." then
		game.command("yaw", 15)
	elseif mode == 2 and key == "-" then
		game.command("scale", 1 / 1.1)
	elseif mode == 2 and key == "=" then
		game.command("scale", 1.1)
	elseif mode == 2 and key == "R" then
		game.command("lift", 0.25)
	elseif mode == 2 and key == "F" then
		game.command("lift", -0.25)
	elseif mode == 2 and (key == "X" or key == "Delete" or key == "Backspace") then
		game.command("delete")
	elseif mode == 2 and key == "PageUp" then
		game.command("page", -1)
	elseif mode == 2 and key == "PageDown" then
		game.command("page", 1)
	else
		local number = tonumber(key)
		if number then
			if mode == 0 and number >= 1 and number <= #tiles then
				game.command("brush", number - 1)
			elseif mode == 1 and number >= 1 and number <= #toolKeys then
				game.command("tool", number - 1)
			elseif mode == 2 then
				game.command("prop", (number + 9) % 10) -- 1〜9は0〜8番、0は9番
			end
		end
	end
end
