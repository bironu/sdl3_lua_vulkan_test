#include "ui/Widget.h"
#include "ui/UiContext.h"
#include <algorithm>
#include <cmath>
#include <atomic>

namespace ui
{

namespace
{
std::atomic<uint32_t> nextId{1};
}

Widget::Widget()
	: id_(nextId++)
{
}

bool Widget::isAncestorOrSelf(const Widget *other) const
{
	for(const Widget *w = this; w; w = w->parent_){
		if(w == other){
			return true;
		}
	}
	return false;
}

bool Widget::add(const std::shared_ptr<Widget> &child)
{
	if(!child || isAncestorOrSelf(child.get())){
		return false; // 自分自身・祖先を子にすると、木が輪になる
	}
	if(child->parent_){
		child->parent_->remove(child);
	}
	child->parent_ = this;
	children_.push_back(child);
	return true;
}

bool Widget::remove(const std::shared_ptr<Widget> &child)
{
	const auto it = std::find(children_.begin(), children_.end(), child);
	if(it == children_.end()){
		return false;
	}
	(*it)->parent_ = nullptr;
	children_.erase(it);
	return true;
}

void Widget::clearChildren()
{
	for(auto &child : children_){
		child->parent_ = nullptr;
	}
	children_.clear();
}

std::shared_ptr<Widget> Widget::find(const std::string &name)
{
	if(name_ == name){
		return shared_from_this();
	}
	for(auto &child : children_){
		if(auto found = child->find(name)){
			return found;
		}
	}
	return nullptr;
}

bool Widget::getProperty(const std::string &name, float &value) const
{
	if(name == "x"){ value = x_; }
	else if(name == "y"){ value = y_; }
	else if(name == "alpha"){ value = alpha_; }
	else if(name == "width"){ value = width_; }
	else if(name == "height"){ value = height_; }
	else if(name == "scale"){ value = scale_; }
	else if(name == "angle"){ value = angleDegrees_; }
	else{ return false; }
	return true;
}

bool Widget::setProperty(const std::string &name, float value)
{
	if(name == "x"){ x_ = value; }
	else if(name == "y"){ y_ = value; }
	else if(name == "alpha"){ alpha_ = value; }
	else if(name == "width"){ width_ = value; }
	else if(name == "height"){ height_ = value; }
	else if(name == "scale"){ scale_ = value; }
	else if(name == "angle"){ angleDegrees_ = value; }
	else{ return false; }
	return true;
}

bool Widget::animate(const std::string &property, float target, float durationSeconds, InterpolatorType interpolator, Loop loop, float delaySeconds)
{
	float current;
	if(!getProperty(property, current)){
		return false;
	}
	stopAnimation(property);
	if(durationSeconds <= 0.0f && delaySeconds <= 0.0f){
		return setProperty(property, target);
	}
	tweens_.push_back({property, current, target, 0.0f, std::max(durationSeconds, 1e-4f), delaySeconds, loop, InterpolatorFactory::create(interpolator)});
	return true;
}

void Widget::stopAnimation(const std::string &property)
{
	tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(), [&](const Tween &t){ return property.empty() || t.property == property; }), tweens_.end());
}

void Widget::updateAnimations(float dt)
{
	for(auto it = tweens_.begin(); it != tweens_.end();){
		if(it->delay > 0.0f){
			it->delay -= dt;
			if(it->delay > 0.0f){
				++it;
				continue;
			}
			dt = -it->delay; // 遅れが終わった後の分から進める
			it->delay = 0.0f;
		}
		it->elapsed += dt;
		const float phase = it->elapsed / it->duration;
		float t;
		bool finished = false;
		switch(it->loop){
		case Loop::Repeat: t = phase - std::floor(phase); break;
		case Loop::PingPong: {
			const float p = std::fmod(phase, 2.0f);
			t = p < 1.0f ? p : 2.0f - p;
			break;
		}
		default: t = std::min(phase, 1.0f); finished = phase >= 1.0f; break;
		}
		setProperty(it->property, it->from + (it->to - it->from) * it->interpolate(t));
		it = finished ? tweens_.erase(it) : std::next(it);
	}
	for(auto &child : children_){
		child->updateAnimations(dt);
	}
}

float Widget::effectiveScale() const
{
	return scale_ * (parent_ ? parent_->effectiveScale() : 1.0f);
}

LayoutRect Widget::resolve(UiContext &ctx, const LayoutRect &parentRect)
{
	// 拡大率: 自分の指定・固有の大きさには、祖先を含めた率を掛ける。親の矩形から決める大きさ(親は掛け済み)と、位置のずらしには、親までの率を掛ける
	const float parentScale = parent_ ? parent_->effectiveScale() : 1.0f;
	const float ownScale = parentScale * scale_;
	float w = width_ * ownScale, h = height_ * ownScale;
	float iw = 0.0f, ih = 0.0f;
	if((width_ <= 0.0f || height_ <= 0.0f) && intrinsicSize(ctx, iw, ih)){
		w = width_ > 0.0f ? w : iw * ownScale;
		h = height_ > 0.0f ? h : ih * ownScale;
	}
	w = w > 0.0f ? w : parentRect.w * scale_;
	h = h > 0.0f ? h : parentRect.h * scale_;
	LayoutRect rect;
	rect.w = w;
	rect.h = h;
	rect.x = parentRect.x + parentRect.w * anchorX_ + x_ * parentScale - w * pivotX_;
	rect.y = parentRect.y + parentRect.h * anchorY_ + y_ * parentScale - h * pivotY_;
	return rect;
}

LayoutRect Widget::resolvedRect(UiContext &ctx)
{
	const LayoutRect parentRect = parent_ ? parent_->resolvedRect(ctx) : LayoutRect{0.0f, 0.0f, ctx.screenWidth(), ctx.screenHeight()};
	return resolve(ctx, parentRect);
}

bool Widget::contains(UiContext &ctx, float x, float y)
{
	const LayoutRect r = resolvedRect(ctx);
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

std::shared_ptr<Widget> Widget::pick(UiContext &ctx, const LayoutRect &parentRect, float x, float y)
{
	if(!visible_){
		return nullptr;
	}
	const LayoutRect rect = resolve(ctx, parentRect);
	for(auto it = children_.rbegin(); it != children_.rend(); ++it){ // 手前(後ろの子)から
		if(auto hit = (*it)->pick(ctx, rect, x, y)){
			return hit;
		}
	}
	if(interactive_ && x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h){
		return shared_from_this();
	}
	return nullptr;
}

void Widget::draw(UiContext &ctx, const LayoutRect &parentRect, float parentAlpha)
{
	if(!visible_){
		return;
	}
	const LayoutRect rect = resolve(ctx, parentRect);
	const float alpha = parentAlpha * alpha_;
	ctx.setCurrentRotation(angleDegrees_ * 3.14159265f / 180.0f);
	drawSelf(ctx, rect, alpha);
	ctx.setCurrentRotation(0.0f);
	for(auto &child : children_){
		child->draw(ctx, rect, alpha);
	}
}

} // namespace ui
