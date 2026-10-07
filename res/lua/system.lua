print("system.lua load.")

window = {
	width=1280,
	height=720,
}

screen = {
	width=1920,
	height=1080,
}

system = {
	lang = "japanese",
	fallback_lang = "english", -- 現在の言語に無い文字列・フォントの代わりに使う言語
	soundfont = "res/sound/Small Soundfont.sf2", -- MIDI(BGM)の音色(SF2。リポジトリ直下からのパス。無ければ空でよい)
--	lang = "english",
}
