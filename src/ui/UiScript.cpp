#include "ui/UiScript.h"
#include "ui/HudWidgets.h"
#include "ui/MenuWidgets.h"
#include "ui/PadNames.h"
#include "sdl/SDLGamepad.h"
#include "resources/ResourcePaths.h"
#include "resources/Resources.h"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <cctype>
#include <sol/sol.hpp>

namespace ui
{

struct UiScript::Impl
{
	sol::state lua;
	std::shared_ptr<GroupWidget> root = std::make_shared<GroupWidget>();
	sol::protected_function fnInit, fnUpdate, fnKey, fnMouseMove, fnMouseButton;
	std::string lastError; // 同じエラーを毎フレームログに出さないため
	// マウスのイベント(hover/click): ウィジェットのid → Luaの関数
	struct Handlers
	{
		sol::protected_function enter, leave, click;
	};
	std::map<uint32_t, Handlers> handlers;
	std::weak_ptr<Widget> hovered; // いま、カーソルが乗っているウィジェット
	std::weak_ptr<Widget> pressed; // ボタンを押したときに乗っていたウィジェット(離したときも同じなら、click)
	bool hasMouse = false;
	float mouseX = 0.0f, mouseY = 0.0f;
};

namespace
{
// find()の結果を、実際の型(Text/Image/...)のハンドルとして返す(基底のWidgetのままだと、setTextなどが使えない)
sol::object toObject(sol::state_view lua, const std::shared_ptr<Widget> &widget)
{
	if(!widget){
		return sol::make_object(lua, sol::lua_nil);
	}
	if(auto p = std::dynamic_pointer_cast<TextWidget>(widget)){
		return sol::make_object(lua, p);
	}
	if(auto p = std::dynamic_pointer_cast<CarouselWidget>(widget)){
		return sol::make_object(lua, p);
	}
	if(auto p = std::dynamic_pointer_cast<StripWidget>(widget)){
		return sol::make_object(lua, p);
	}
	if(auto p = std::dynamic_pointer_cast<RectWidget>(widget)){
		return sol::make_object(lua, p);
	}
	if(auto p = std::dynamic_pointer_cast<MinimapWidget>(widget)){
		return sol::make_object(lua, p);
	}
	if(auto p = std::dynamic_pointer_cast<ImageWidget>(widget)){
		return sol::make_object(lua, p);
	}
	if(auto p = std::dynamic_pointer_cast<BackgroundWidget>(widget)){
		return sol::make_object(lua, p);
	}
	if(auto p = std::dynamic_pointer_cast<GroupWidget>(widget)){
		return sol::make_object(lua, p);
	}
	return sol::make_object(lua, widget);
}

// Luaのハンドル(派生型のshared_ptr)から、基底のshared_ptr<Widget>を取り出す(sol2は、shared_ptrの基底への暗黙変換を引数で受けられないため、型を順に調べる)
std::shared_ptr<Widget> toWidget(const sol::object &object)
{
	if(object.is<std::shared_ptr<TextWidget>>()){
		return object.as<std::shared_ptr<TextWidget>>();
	}
	if(object.is<std::shared_ptr<CarouselWidget>>()){
		return object.as<std::shared_ptr<CarouselWidget>>();
	}
	if(object.is<std::shared_ptr<StripWidget>>()){
		return object.as<std::shared_ptr<StripWidget>>();
	}
	if(object.is<std::shared_ptr<RectWidget>>()){
		return object.as<std::shared_ptr<RectWidget>>();
	}
	if(object.is<std::shared_ptr<MinimapWidget>>()){
		return object.as<std::shared_ptr<MinimapWidget>>();
	}
	if(object.is<std::shared_ptr<ImageWidget>>()){
		return object.as<std::shared_ptr<ImageWidget>>();
	}
	if(object.is<std::shared_ptr<BackgroundWidget>>()){
		return object.as<std::shared_ptr<BackgroundWidget>>();
	}
	if(object.is<std::shared_ptr<GroupWidget>>()){
		return object.as<std::shared_ptr<GroupWidget>>();
	}
	return nullptr;
}

// Luaのテーブル(キー→文字列または数値)を、{名前}の差し込み用のmapにする
std::map<std::string, std::string> tableToStrings(const sol::table &table)
{
	std::map<std::string, std::string> result;
	for(const auto &entry : table){
		if(!entry.first.is<std::string>()){
			continue;
		}
		std::string value;
		if(entry.second.is<std::string>()){
			value = entry.second.as<std::string>();
		}
		else if(entry.second.is<double>()){
			const double number = entry.second.as<double>();
			value = number == static_cast<long long>(number) ? std::to_string(static_cast<long long>(number)) : std::to_string(number);
		}
		else{
			continue;
		}
		result[entry.first.as<std::string>()] = value;
	}
	return result;
}

// 補間の名前(task/Interpolator.hの種類。大文字小文字は問わない)。旧名のeaseIn/easeOut/easeInOutも使える。分からなければfalse
bool parseInterpolator(const std::string &name, InterpolatorType &type)
{
	std::string n;
	for(char c : name){
		n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	static const std::pair<const char *, InterpolatorType> table[] = {
		{"accelerateDecelerate", InterpolatorType::AccelerateDecelerate}, {"easeinout", InterpolatorType::AccelerateDecelerate},
		{"accelerate", InterpolatorType::Accelerate}, {"easein", InterpolatorType::Accelerate},
		{"decelerate", InterpolatorType::Decelerate}, {"easeout", InterpolatorType::Decelerate},
		{"anticipate", InterpolatorType::Anticipate}, {"anticipateOvershoot", InterpolatorType::AnticipateOvershoot},
		{"bounce", InterpolatorType::Bounce}, {"cycle", InterpolatorType::Cycle},
		{"linear", InterpolatorType::Linear}, {"overshoot", InterpolatorType::Overshoot},
	};
	for(const auto &entry : table){
		std::string key;
		for(const char *c = entry.first; *c; ++c){
			key += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
		}
		if(key == n){
			type = entry.second;
			return true;
		}
	}
	return false;
}

float byteToUnit(int value)
{
	return static_cast<float>(value) / 255.0f;
}
}

UiScript::UiScript(UiContext &ctx, Callbacks callbacks)
	: ctx_(ctx)
	, callbacks_(std::move(callbacks))
{
}

UiScript::~UiScript() = default;

bool UiScript::load(const std::string &path)
{
	impl_ = std::make_unique<Impl>();
	Impl &impl = *impl_;
	sol::state &lua = impl.lua;
	lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8);
	UiContext *ctx = &ctx_;

	// ウィジェットのハンドル(Widgetと、その派生)
	auto widgetType = lua.new_usertype<Widget>("Widget", sol::no_constructor,
		"getId", &Widget::id,
		"setName", &Widget::setName, "getName", &Widget::name,
		"setPos", &Widget::setPos, "getX", &Widget::x, "getY", &Widget::y,
		"setSize", &Widget::setSize,
		"setAnchor", &Widget::setAnchor, "setPivot", &Widget::setPivot,
		"setVisible", &Widget::setVisible, "isVisible", &Widget::visible,
		"setAlpha", &Widget::setAlpha, "getAlpha", &Widget::alpha,
			"setScale", &Widget::setScale, "getScale", &Widget::scale,
			"setAngle", &Widget::setAngle, "getAngle", &Widget::angle,
		"add", [](Widget &w, const sol::object &child){ return w.add(toWidget(child)); },
		"remove", [](Widget &w, const sol::object &child){ return w.remove(toWidget(child)); },
		"clear", &Widget::clearChildren,
		// animate(プロパティ, 目標, 秒, 補間, {loop = "once"/"repeat"/"pingpong", delay = 秒})。補間は省略でaccelerateDecelerate。四番目にテーブルだけを渡してもよい
			"animate", [](Widget &w, const std::string &property, float target, sol::optional<float> seconds, sol::optional<sol::object> interpolator, sol::optional<sol::table> options){
				InterpolatorType type = InterpolatorType::AccelerateDecelerate;
				sol::optional<sol::table> opts = options;
				if(interpolator){
					if(interpolator->is<std::string>()){
						if(!parseInterpolator(interpolator->as<std::string>(), type)){
							SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ui: unknown interpolator: %s", interpolator->as<std::string>().c_str());
						}
					}
					else if(interpolator->is<sol::table>()){
						opts = interpolator->as<sol::table>();
					}
				}
				Loop loop = Loop::Once;
				float delay = 0.0f;
				if(opts){
					const std::string mode = opts->get_or<std::string>("loop", "once");
					loop = mode == "repeat" ? Loop::Repeat : mode == "pingpong" ? Loop::PingPong : Loop::Once;
					delay = opts->get_or("delay", 0.0f);
				}
				return w.animate(property, target, seconds.value_or(0.0f), type, loop, delay);
			},
			"stopAnimation", [](Widget &w, sol::optional<std::string> property){ w.stopAnimation(property.value_or("")); },
			"isAnimating", &Widget::isAnimating,
		"find", [&lua](Widget &w, const std::string &name){ return toObject(lua, w.find(name)); },
		// 解決後の大きさ・位置(文字・画像の大きさ、親のアンカーなどを含む)
		// 大きさの指定(setSize)に依らない、文字・画像そのものの大きさ(無ければ0)
		"getNaturalWidth", [ctx](Widget &w){ float nw = 0.0f, nh = 0.0f; w.naturalSize(*ctx, nw, nh); return nw; },
		"getNaturalHeight", [ctx](Widget &w){ float nw = 0.0f, nh = 0.0f; w.naturalSize(*ctx, nw, nh); return nh; },
		"getWidth", [ctx](Widget &w){ return w.resolvedRect(*ctx).w; },
		"getHeight", [ctx](Widget &w){ return w.resolvedRect(*ctx).h; },
		"getScreenX", [ctx](Widget &w){ return w.resolvedRect(*ctx).x; },
		"getScreenY", [ctx](Widget &w){ return w.resolvedRect(*ctx).y; });
	// マウスの判定: onEnter(fn)/onLeave(fn)/onClick(fn)で、関数(引数: ウィジェット。onClickはさらにボタン番号)を設定する(nilで解除。設定したウィジェットは自動でinteractive)
	Impl *self = &impl;
	const auto setHandler = [self, &lua](Widget &w, sol::protected_function Impl::Handlers::*member, const sol::object &fn){
		auto &entry = self->handlers[w.id()];
		entry.*member = fn.is<sol::function>() ? sol::protected_function(fn.as<sol::function>()) : sol::protected_function();
		w.setInteractive(true);
	};
	widgetType["contains"] = [ctx](Widget &w, float x, float y){ return w.contains(*ctx, x, y); };
	widgetType["setInteractive"] = &Widget::setInteractive;
	widgetType["isInteractive"] = &Widget::interactive;
	widgetType["isHovered"] = &Widget::hovered;
	widgetType["onEnter"] = [setHandler](Widget &w, const sol::object &fn){ setHandler(w, &Impl::Handlers::enter, fn); };
	widgetType["onLeave"] = [setHandler](Widget &w, const sol::object &fn){ setHandler(w, &Impl::Handlers::leave, fn); };
	widgetType["onClick"] = [setHandler](Widget &w, const sol::object &fn){ setHandler(w, &Impl::Handlers::click, fn); };
	lua.new_usertype<GroupWidget>("GroupWidget", sol::no_constructor, sol::base_classes, sol::bases<Widget>());
	lua.new_usertype<TextWidget>("TextWidget", sol::no_constructor, sol::base_classes, sol::bases<Widget>(),
		"setText", &TextWidget::setText, "getText", &TextWidget::text,
		// 多言語: キー(lang/*.luaのstrings)で文字列を指定する。言語が切り替わると自動で更新される。varsは{名前}の差し込み(テーブル)
		"setTextKey", &TextWidget::setTextKey, "getTextKey", &TextWidget::textKey,
		"setTextVars", [](TextWidget &w, const sol::table &vars){ w.setTextVars(tableToStrings(vars)); },
		"setFont", &TextWidget::setFont, "setFontSize", &TextWidget::setFontSize,
		// 色は0〜255
		"setColor", [](TextWidget &w, int r, int g, int b, sol::optional<int> a){ w.setColor(byteToUnit(r), byteToUnit(g), byteToUnit(b), byteToUnit(a.value_or(255))); },
		// ビットマップフォントで描く(ASCIIだけ。毎フレーム変わる数字向き)
		"setBitmap", &TextWidget::setBitmap,
		// 縁取り: setOutline(太さ(ピクセル。0で無し), R, G, B, [A])
		"setOutline", [](TextWidget &w, float width, sol::optional<int> r, sol::optional<int> g, sol::optional<int> b, sol::optional<int> a){
			w.setOutline(width, byteToUnit(r.value_or(0)), byteToUnit(g.value_or(0)), byteToUnit(b.value_or(0)), byteToUnit(a.value_or(255)));
		});
	lua.new_usertype<CarouselWidget>("CarouselWidget", sol::no_constructor, sol::base_classes, sol::bases<Widget>(),
		"setItems", [](CarouselWidget &w, const sol::table &paths){ std::vector<std::string> list; for(const auto &entry : paths){ if(entry.second.is<std::string>()){ list.push_back(entry.second.as<std::string>()); } } w.setItems(list); },
		"getItemCount", &CarouselWidget::itemCount,
		"setRadius", &CarouselWidget::setRadius, "setTilt", &CarouselWidget::setTilt, "setViewAngle", &CarouselWidget::setViewAngle,
		"setRingDistance", &CarouselWidget::setRingDistance, "setItemSize", &CarouselWidget::setItemSize,
		"setItemAlpha", &CarouselWidget::setItemAlpha, "setSelectedOffset", &CarouselWidget::setSelectedOffset, "setBillboard", &CarouselWidget::setBillboard,
		// 選択中の番号(0始まり。項目が無ければ-1)。Luaの配列に合わせて、+1した番号は、スクリプト側で
		"getSelected", &CarouselWidget::selected,
		"rotate", [](CarouselWidget &w, int delta, sol::optional<float> seconds){ w.rotate(delta, seconds.value_or(0.0f)); },
		"select", [](CarouselWidget &w, int index, sol::optional<float> seconds){ w.select(index, seconds.value_or(0.0f)); });
	lua.new_usertype<StripWidget>("StripWidget", sol::no_constructor, sol::base_classes, sol::bases<Widget>(),
		"setItems", [](StripWidget &w, const sol::table &paths){ std::vector<std::string> list; for(const auto &entry : paths){ if(entry.second.is<std::string>()){ list.push_back(entry.second.as<std::string>()); } } w.setItems(list); },
		"getItemCount", &StripWidget::itemCount,
		"setItemSize", &StripWidget::setItemSize, "setGap", &StripWidget::setGap,
		"setBackground", [](StripWidget &w, float r, float g, float b, float a){ w.setBackground(r, g, b, a); },
		"setBackgroundAlpha", &StripWidget::setBackgroundAlpha,
		"getSelected", &StripWidget::selected,
		"select", [](StripWidget &w, int index, sol::optional<float> seconds){ w.select(index, seconds.value_or(0.0f)); });
	lua.new_usertype<ImageWidget>("ImageWidget", sol::no_constructor, sol::base_classes, sol::bases<Widget>(),
		"setImage", &ImageWidget::setImage, "getImage", &ImageWidget::image,
		"setTint", [](ImageWidget &w, int r, int g, int b, sol::optional<int> a){ w.setTint(byteToUnit(r), byteToUnit(g), byteToUnit(b), byteToUnit(a.value_or(255))); });
	lua.new_usertype<RectWidget>("RectWidget", sol::no_constructor, sol::base_classes, sol::bases<Widget>(),
		"setColor", [](RectWidget &w, int r, int g, int b, sol::optional<int> a){ w.setColor(byteToUnit(r), byteToUnit(g), byteToUnit(b), byteToUnit(a.value_or(255))); },
		"setBorder", [](RectWidget &w, float width, int r, int g, int b, sol::optional<int> a){ w.setBorder(width, byteToUnit(r), byteToUnit(g), byteToUnit(b), byteToUnit(a.value_or(255))); });
	lua.new_usertype<MinimapWidget>("MinimapWidget", sol::no_constructor, sol::base_classes, sol::bases<Widget>(),
		"setRange", &MinimapWidget::setRange, "setMarkerSize", &MinimapWidget::setMarkerSize,
		"setBackground", [](MinimapWidget &w, int r, int g, int b, int a){ w.setBackground(byteToUnit(r), byteToUnit(g), byteToUnit(b), byteToUnit(a)); },
		"setBorderColor", [](MinimapWidget &w, int r, int g, int b, int a){ w.setBorder(byteToUnit(r), byteToUnit(g), byteToUnit(b), byteToUnit(a)); });
	lua.new_usertype<BackgroundWidget>("BackgroundWidget", sol::no_constructor, sol::base_classes, sol::bases<Widget>(),
		"setImage", &BackgroundWidget::setImage,
		"setColor", [](BackgroundWidget &w, int r, int g, int b, sol::optional<int> a){ w.setColor(byteToUnit(r), byteToUnit(g), byteToUnit(b), byteToUnit(a.value_or(255))); });

	// ウィジェットを作る関数
	sol::table uiTable = lua.create_named_table("ui");
	uiTable["group"] = []{ return std::make_shared<GroupWidget>(); };
	uiTable["carousel"] = []{ return std::make_shared<CarouselWidget>(); };
	uiTable["strip"] = []{ return std::make_shared<StripWidget>(); };
	uiTable["rect"] = []{ return std::make_shared<RectWidget>(); };
	uiTable["minimap"] = []{ return std::make_shared<MinimapWidget>(); };
	uiTable["background"] = []{ return std::make_shared<BackgroundWidget>(); };
	uiTable["image"] = [](sol::optional<std::string> path){
		auto w = std::make_shared<ImageWidget>();
		if(path){
			w->setImage(*path);
		}
		return w;
	};
	// ui.text(文字列, 大きさ) または ui.text(文字列, {size = 大きさ, font = フォント名, key = 多言語のキー})。文字列はnilでもよい
	uiTable["text"] = [](sol::optional<std::string> text, sol::optional<sol::object> options){
		auto w = std::make_shared<TextWidget>();
		w->setText(text.value_or(""));
		if(options){
			if(options->is<double>()){
				w->setFontSize(static_cast<float>(options->as<double>()));
			}
			else if(options->is<sol::table>()){
				const sol::table table = options->as<sol::table>();
				if(const auto size = table.get<sol::optional<float>>("size")){
					w->setFontSize(*size);
				}
				if(const auto font = table.get<sol::optional<std::string>>("font")){
					w->setFont(*font);
				}
				if(const auto key = table.get<sol::optional<std::string>>("key")){
					w->setTextKey(*key);
				}
				if(table.get_or("bitmap", false)){
					w->setBitmap(true);
				}
			}
		}
		return w;
	};
	// 多言語: i18n.t(key, vars) 文字列、i18n.language() 現在の言語、i18n.setLanguage(name) 切り替え(成否を返す。TextWidgetは自動で更新)、i18n.languages() 選べる言語
	Resources *resources = &ctx_.resources();
	sol::table i18nTable = lua.create_named_table("i18n");
	i18nTable["t"] = [resources](const std::string &key, sol::optional<sol::table> vars){
		return resources->translate(key, vars ? tableToStrings(*vars) : std::map<std::string, std::string>{});
	};
	i18nTable["language"] = [resources]{ return resources->language(); };
	i18nTable["setLanguage"] = [resources](const std::string &name){ return resources->setLanguage(name); };
	i18nTable["languages"] = [resources, &lua]{ return sol::as_table(resources->availableLanguages()); };
	lua["root"] = impl.root;
	impl.root->setSize(ctx_.screenWidth(), ctx_.screenHeight());

	// シーン・画面・入力・ログ
	sol::table sceneTable = lua.create_named_table("scene");
	sceneTable["change"] = [this](const std::string &name){ if(callbacks_.changeScene){ callbacks_.changeScene(name); } };
	sceneTable["quit"] = [this]{ if(callbacks_.quit){ callbacks_.quit(); } };
	sol::table screenTable = lua.create_named_table("screen");
	screenTable["width"] = ctx_.screenWidth();
	screenTable["height"] = ctx_.screenHeight();
	// world: ゲームの状態(読み取り専用。毎フレーム、updateの前に更新される): x, z, heading, cameraYaw, fps
	lua.create_named_table("world");
	sol::table gameTable = lua.create_named_table("game");
	gameTable["command"] = [this](const std::string &name, sol::optional<double> value){
		if(callbacks_.command){
			callbacks_.command(name, value.value_or(0.0));
		}
	};
	sol::table inputTable = lua.create_named_table("input");
	Resources *inputResources = &ctx_.resources();
	inputTable["isDown"] = [inputResources](const std::string &name){
		SDL_GamepadButton button;
		if(padButtonFromName(name, button)){
			const auto pad = inputResources->getGamepad();
			return pad && pad->button(button);
		}
		const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
		const bool *keys = SDL_GetKeyboardState(nullptr);
		return code != SDL_SCANCODE_UNKNOWN && keys[code];
	};
	inputTable["axis"] = [inputResources](const std::string &name){
		const auto pad = inputResources->getGamepad();
		if(!pad){
			return 0.0f;
		}
		float x, y;
		if(name == "LeftX" || name == "LeftY"){
			pad->leftStick(x, y);
			return name == "LeftX" ? x : y;
		}
		if(name == "RightX" || name == "RightY"){
			pad->rightStick(x, y);
			return name == "RightX" ? x : y;
		}
		if(name == "LeftTrigger"){
			return pad->axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
		}
		if(name == "RightTrigger"){
			return pad->axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
		}
		return 0.0f;
	};
	sol::table audioTable = lua.create_named_table("audio");
	audioTable["play"] = [this](const std::string &path){ if(callbacks_.playSound){ callbacks_.playSound(path); } };
	// 別のLuaファイルを、同じ状態で実行する(定義だけを書いたデータファイルを読むのに使う)。失敗したらエラーになる
	lua["loadScript"] = [&lua](const std::string &scriptPath){
		return lua.script_file(ResourcePaths::resource(scriptPath.c_str()));
	};
	const auto logFunction = [&lua](sol::variadic_args args){
		std::string line;
		for(auto arg : args){
			sol::function tostring = lua["tostring"];
			line += tostring(arg).get<std::string>();
			line += "\t";
		}
		SDL_Log("[lua] %s", line.c_str());
	};
	lua["log"] = logFunction;
	lua["print"] = logFunction;

	pushWorld(); // スクリプトの実行中(トップレベル・init())から、worldを読めるように
	// スクリプトを実行して、定義された関数を集める
	const std::string fullPath = ResourcePaths::resource(path.c_str());
	const auto result = lua.safe_script_file(fullPath, sol::script_pass_on_error);
	if(!result.valid()){
		const sol::error error = result;
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ui: script error (%s): %s", path.c_str(), error.what());
		return false;
	}
	impl.fnInit = lua["init"];
	impl.fnUpdate = lua["update"];
	impl.fnKey = lua["onKey"];
	impl.fnMouseMove = lua["onMouseMove"];
	impl.fnMouseButton = lua["onMouseButton"];
	if(impl.fnInit.valid()){
		const auto initResult = impl.fnInit();
		if(!initResult.valid()){
			const sol::error error = initResult;
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ui: init() error (%s): %s", path.c_str(), error.what());
			return false;
		}
	}
	return true;
}

namespace
{
// 呼び出して、エラーならログに出す(同じ内容は続けて出さない)
template<typename... Args>
void callSafely(sol::protected_function &fn, std::string &lastError, const char *name, Args &&...args)
{
	if(!fn.valid()){
		return;
	}
	const auto result = fn(std::forward<Args>(args)...);
	if(!result.valid()){
		const sol::error error = result;
		if(lastError != error.what()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "ui: %s() error: %s", name, error.what());
			lastError = error.what();
		}
	}
}
}

void UiScript::pushWorld()
{
	const WorldView &view = ctx_.world();
	sol::table world = impl_->lua["world"];
	world["x"] = view.playerX;
	world["z"] = view.playerZ;
	world["heading"] = view.heading;
	world["cameraYaw"] = view.cameraYaw;
	world["fps"] = view.fps;
	world["sensitivity"] = view.sensitivity;
	for(const auto &entry : view.values){
		world[entry.first] = entry.second;
	}
	sol::table strings = impl_->lua.create_table();
	for(const auto &entry : view.strings){
		strings[entry.first] = entry.second;
	}
	world["strings"] = strings;
}

// いまのマウスの位置にある、最前面のinteractiveなウィジェットを求めて、enter/leaveを呼ぶ
void UiScript::updateHover()
{
	Impl &impl = *impl_;
	std::shared_ptr<Widget> now;
	if(impl.hasMouse){
		now = impl.root->pick(ctx_, LayoutRect{0.0f, 0.0f, ctx_.screenWidth(), ctx_.screenHeight()}, impl.mouseX, impl.mouseY);
	}
	const auto before = impl.hovered.lock();
	if(now == before){
		return;
	}
	const auto call = [&](const std::shared_ptr<Widget> &w, sol::protected_function Impl::Handlers::*member){
		const auto found = impl.handlers.find(w->id());
		if(found != impl.handlers.end() && (found->second.*member).valid()){
			callSafely(found->second.*member, impl.lastError, "onEnter/onLeave", toObject(impl.lua, w));
		}
	};
	impl.hovered = now;
	if(before){
		before->setHovered(false);
		call(before, &Impl::Handlers::leave);
	}
	if(now){
		now->setHovered(true);
		call(now, &Impl::Handlers::enter);
	}
}

void UiScript::update(float dt, float time)
{
	if(impl_){
		updateHover(); // ウィジェットが動いた・出た・消えた場合も、マウスが動かなくても反映する
		impl_->root->updateAnimations(dt); // アニメーション(animate)を進めてから、スクリプトのupdateへ
		pushWorld();
		callSafely(impl_->fnUpdate, impl_->lastError, "update", dt, time);
	}
}

void UiScript::onKey(const std::string &keyName, bool down)
{
	if(impl_){
		callSafely(impl_->fnKey, impl_->lastError, "onKey", keyName, down);
	}
}

void UiScript::onMouseMove(float x, float y)
{
	if(impl_){
		impl_->hasMouse = true;
		impl_->mouseX = x;
		impl_->mouseY = y;
		updateHover();
		callSafely(impl_->fnMouseMove, impl_->lastError, "onMouseMove", x, y);
	}
}

void UiScript::onMouseButton(int button, bool down, float x, float y)
{
	if(impl_){
		impl_->hasMouse = true;
		impl_->mouseX = x;
		impl_->mouseY = y;
		updateHover();
		if(down){
			impl_->pressed = impl_->hovered;
		}
		else if(const auto target = impl_->pressed.lock()){
			impl_->pressed.reset();
			if(target == impl_->hovered.lock()){ // 押したウィジェットの上で離したら、click
				const auto found = impl_->handlers.find(target->id());
				if(found != impl_->handlers.end()){
					callSafely(found->second.click, impl_->lastError, "onClick", toObject(impl_->lua, target), button);
				}
			}
		}
		callSafely(impl_->fnMouseButton, impl_->lastError, "onMouseButton", button, down, x, y);
	}
}

bool UiScript::hasHover() const
{
	return impl_ && !impl_->hovered.expired();
}

void UiScript::draw()
{
	if(impl_){
		impl_->root->draw(ctx_, LayoutRect{0.0f, 0.0f, ctx_.screenWidth(), ctx_.screenHeight()}, 1.0f);
	}
}

} // namespace ui
