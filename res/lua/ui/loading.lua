-- LoadingScene: 白い文字 "Now Loading..." が、画面の左下から右下まで往復する。
-- (GameSceneの読み込みは、C++が別スレッドで行い、終わったら自動でGameSceneへ進む)

local speed = 700 -- 文字の移動速度(論理画面の座標/秒)
local bottomMargin = 60
local text

function init()
	local background = ui.background()
	background:setColor(0, 0, 0)
	root:add(background)

	text = ui.text(nil, {size = 72, key = "NowLoading"})
	text:setColor(255, 255, 255)
	text:setAnchor(0, 1) -- 親(画面)の左下を基準に
	text:setPivot(0, 1) -- 自分の左下を合わせる
	root:add(text)
end

function update(dt, time)
	-- 左端〜右端を往復する(三角波)
	local range = math.max(screen.width - text:getWidth(), 1)
	local phase = (time * speed) % (range * 2)
	local x = phase < range and phase or range * 2 - phase
	text:setPos(x, -bottomMargin)
end

function onKey(key, down)
	if down and key == "Escape" then
		scene.quit()
	end
end
