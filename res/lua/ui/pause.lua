-- GameScene のポーズ画面(Esc / Start で開く。開くたびに、このスクリプトが読み直される)。
-- 操作: 上下で選ぶ、Enter / PadA で決定、左右で値の変更、Esc / Start / PadB で再開。マウスは、乗せると選択、クリックで決定。
-- ゲームへの命令は game.command(名前, 値)。名前はGameSceneの command() が受ける("resume", "sensitivity")

local itemSize = 56
local itemGap = 84
local sensitivityStep = 0.25
local items = {}
local selected = 1

local function languageName()
	return i18n.t("LanguageName")
end

local function formatSensitivity(value)
	return string.format("%.2f", value)
end

local function refreshItem(item)
	if item.vars then
		item.text:setTextVars(item.vars())
	end
end

-- 言語を、選べる言語の順に、delta(+1/-1)だけ切り替える
local function changeLanguage(delta)
	local languages = i18n.languages()
	for i, name in ipairs(languages) do
		if name == i18n.language() then
			i18n.setLanguage(languages[(i - 1 + delta) % #languages + 1])
			break
		end
	end
	for _, item in ipairs(items) do
		refreshItem(item)
	end
end

local sensitivity = world.sensitivity

local function changeSensitivity(delta)
	sensitivity = math.max(0.25, math.min(3.0, sensitivity + delta * sensitivityStep))
	game.command("sensitivity", sensitivity)
	for _, item in ipairs(items) do
		refreshItem(item)
	end
end

local function select(index)
	selected = (index - 1) % #items + 1
	for i, item in ipairs(items) do
		local current = i == selected
		item.text:setColor(current and 255 or 210, current and 220 or 210, current and 90 or 210)
		item.text:animate("scale", current and 1.15 or 1, 0.12, "decelerate")
	end
end

local definitions = {
	{key = "PauseResume", activate = function() game.command("resume") end},
	{key = "PauseLanguage", vars = function() return {name = languageName()} end,
		activate = function() changeLanguage(1) end, step = changeLanguage},
	{key = "PauseSensitivity", vars = function() return {value = formatSensitivity(sensitivity)} end,
		activate = function() changeSensitivity(sensitivity >= 3.0 and -11 or 1) end, step = changeSensitivity},
	{key = "PauseToTitle", activate = function() scene.change("opening") end},
	{key = "PauseQuit", activate = function() scene.quit() end},
}

function init()
	local dim = ui.rect()
	dim:setColor(0, 0, 0, 160)
	root:add(dim)

	local title = ui.text(nil, {size = 84, key = "PauseTitle"})
	title:setColor(255, 255, 255)
	title:setAnchor(0.5, 0.5)
	title:setPivot(0.5, 0.5)
	title:setPos(0, -(#definitions * itemGap) / 2 - 90)
	root:add(title)

	for i, def in ipairs(definitions) do
		local text = ui.text(nil, {size = itemSize, key = def.key})
		text:setAnchor(0.5, 0.5)
		text:setPivot(0.5, 0.5)
		text:setPos(0, (i - (#definitions + 1) / 2) * itemGap + 30)
		root:add(text)
		local item = {text = text, activate = def.activate, step = def.step, vars = def.vars}
		items[i] = item
		refreshItem(item)
		text:onEnter(function() select(i) end)
		text:onClick(function()
			select(i)
			item.activate()
		end)
	end
	select(1)
end

function onKey(key, down)
	if not down then
		return
	end
	if key == "Up" or key == "PadUp" then
		select(selected - 1)
	elseif key == "Down" or key == "PadDown" then
		select(selected + 1)
	elseif key == "Left" or key == "PadLeft" then
		if items[selected].step then items[selected].step(-1) end
	elseif key == "Right" or key == "PadRight" then
		if items[selected].step then items[selected].step(1) end
	elseif key == "Return" or key == "Space" or key == "PadA" then
		items[selected].activate()
	elseif key == "PadB" then
		game.command("resume")
	end
end
