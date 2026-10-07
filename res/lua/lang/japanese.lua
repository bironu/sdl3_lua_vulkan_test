print("japanese.lua load.")

-- フォント: 名前で定義する(ui.text(..., {font = "名前"}) / TextWidget:setFont)。defaultは必須。sizeは、大きさを指定しないときの既定
fonts = {
	default = { file = "res/font/ipaexm.ttf", size = 48 },
}

-- 文字列: キーで引く(C++: Resources::translate / Lua: i18n.t、TextWidget:setTextKey)。{名前}は、差し込み用
strings = {
	LanguageName = "日本語",
	PushEnter = "Push Enter",
	NowLoading = "Now Loading...",
	TitleOpning = "SDL3 動作確認",
	HudFps = "FPS {fps}",
	HudPosition = "X {x}  Z {z}",
	HudHelpTitle = "操作 (Hで表示切替)",
	HudHelpMove = "WASD / 左スティック: 移動",
	HudHelpCamera = "マウス / 右スティック: 視点",
	HudHelpZoom = "ホイール: カメラの距離",
	HudHelpMap = "M: ミニマップ切替",
	HudHelpQuit = "Esc / Start: ポーズ",
	PauseTitle = "ポーズ",
	PauseResume = "再開",
	PauseLanguage = "言語: {name}",
	PauseSensitivity = "視点の感度: {value}",
	PauseToTitle = "タイトルへ戻る",
	PauseQuit = "ゲームを終了",
	TileGrass = "草地",
	TileSoil = "土",
	TileAsphalt = "アスファルト",
	TileStone = "石畳",
	TileSand = "砂",
	TileWater = "水",
	EditSaved = "保存しました",
	EditHelpPaint = "左ドラッグ: タイルを置く",
	EditHelpCamera = "右ドラッグ: 回転 / ホイール: ズーム",
	EditHelpMove = "W A S D: 移動 / Q E: 回転",
	EditHelpBrush = "1-6: タイル選択",
	EditHelpSave = "Esc: メニュー(保存・終了など)",
	EditHelpUndo = "Z: 元に戻す",
	CharaHint = "Enter / A: このキャラで始める",
	CharaFailed = "読み込めませんでした",
	EditMenuTitle = "メニュー",
	EditMenuResume = "編集に戻る",
	EditMenuSave = "保存",
	EditMenuReload = "保存したものを読み直す",
	EditMenuFill = "全面を選んだタイルで塗る",
	EditMenuQuit = "エディタを終了",
}
