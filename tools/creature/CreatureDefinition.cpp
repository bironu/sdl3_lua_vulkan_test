#include "creature/CreatureDefinition.h"
#include "resources/LuaTable.h"

namespace creature
{

namespace
{
// 体の形(body / face / legMesh / legs / color の表)
model::CreatureSpec readSpec(const sol::table &t)
{
	model::CreatureSpec spec;
	const sol::optional<sol::table> body = t["body"], face = t["face"], legMesh = t["legMesh"], color = t["color"];
	readField(body, "length", spec.body.length);
	readField(body, "width", spec.body.width);
	readField(body, "height", spec.body.height);
	readField(body, "taper", spec.body.taper);
	readField(body, "slices", spec.body.slices);
	readField(body, "sides", spec.body.sides);
	readField(face, "pitch", spec.face.pitch);
	readField(face, "lift", spec.face.lift);
	readField(face, "eyeSpacing", spec.face.eyeSpacing);
	readField(face, "eyeUp", spec.face.eyeUp);
	readField(face, "eyeRadius", spec.face.eyeRadius);
	readField(face, "eyeSpan", spec.face.eyeSpan);
	readField(face, "eyeThickness", spec.face.eyeThickness);
	readField(face, "eyeSegments", spec.face.eyeSegments);
	readField(face, "mouthUp", spec.face.mouthUp);
	readField(face, "mouthWidth", spec.face.mouthWidth);
	readField(face, "mouthHeight", spec.face.mouthHeight);
	readField(face, "mouthSides", spec.face.mouthSides);
	readField(legMesh, "sides", spec.legMesh.sides);
	readField(legMesh, "rings", spec.legMesh.rings);
	readField(legMesh, "blend", spec.legMesh.blend);
	readField(legMesh, "inset", spec.legMesh.inset);
	readArray(color, "back", spec.color.back);
	readArray(color, "belly", spec.color.belly);
	readArray(color, "legs", spec.color.legs);
	readArray(color, "face", spec.color.face);
	if(const sol::optional<sol::table> legs = t["legs"]){
		for(size_t i = 1; i <= legs->size(); ++i){
			const sol::optional<sol::table> entry = (*legs)[i];
			if(!entry){
				continue;
			}
			model::CreatureSpec::Leg leg;
			readField(entry, "name", leg.name);
			readField(entry, "along", leg.along);
			readField(entry, "angle", leg.angle);
			readArray(entry, "knee", leg.knee);
			readArray(entry, "ankle", leg.ankle);
			readArray(entry, "foot", leg.foot);
			readArray(entry, "radius", leg.radius);
			spec.legs.push_back(std::move(leg));
		}
	}
	return spec;
}

// 潰れ(attackStand / attackJump の squash の表)
void readSquash(const sol::optional<sol::table> &motion, game::CreatureMotion::Squash &squash)
{
	if(!motion){
		return;
	}
	const sol::optional<sol::table> t = (*motion)["squash"];
	readField(t, "amount", squash.amount);
	readField(t, "stretch", squash.stretch);
	readField(t, "duration", squash.duration);
	readField(t, "bounce", squash.bounce);
}

void readWalk(const sol::optional<sol::table> &walk, game::CreatureMotion::Walk &w)
{
	readField(walk, "speed", w.speed);
	readField(walk, "stride", w.stride);
	readField(walk, "duty", w.duty);
	readField(walk, "swing", w.swing);
	readField(walk, "lift", w.lift);
	readField(walk, "kneeLift", w.kneeLift);
	readField(walk, "bob", w.bob);
	readField(walk, "hop", w.hop);
	readField(walk, "nod", w.nod);
	readField(walk, "sway", w.sway);
	readField(walk, "roll", w.roll);
}

// 動き(walkFast / walkSlow / idle / attackStand / attackJump / death の表)
game::CreatureMotion readMotion(const sol::table &t)
{
	game::CreatureMotion motion;
	const sol::optional<sol::table> idle = t["idle"], stand = t["attackStand"], jump = t["attackJump"], death = t["death"];
	readWalk(t["walkFast"], motion.walkFast);
	readWalk(t["walkSlow"], motion.walkSlow);
	readField(idle, "period", motion.idle.period);
	readField(idle, "breath", motion.idle.breath);
	readField(idle, "pitch", motion.idle.pitch);
	readField(idle, "frontSwing", motion.idle.frontSwing);
	auto &a = motion.stand;
	readField(stand, "rise", a.rise);
	readField(stand, "angle", a.angle);
	readField(stand, "frontRaise", a.frontRaise);
	readField(stand, "frontFold", a.frontFold);
	readField(stand, "wiggle", a.wiggle);
	readField(stand, "wiggleCount", a.wiggleCount);
	readField(stand, "wiggleSwing", a.wiggleSwing);
	readField(stand, "wiggleKnee", a.wiggleKnee);
	readField(stand, "fall", a.fall);
	readField(stand, "fallEase", a.fallEase);
	readField(stand, "fallAngle", a.fallAngle);
	readField(stand, "reach", a.reach);
	readField(stand, "reachOpen", a.reachOpen);
	readField(stand, "reachKnee", a.reachKnee);
	readField(stand, "bounce", a.bounce);
	readField(stand, "hold", a.hold);
	readField(stand, "recover", a.recover);
	readSquash(stand, a.squash);
	auto &j = motion.jump;
	readField(jump, "crouch", j.crouch);
	readField(jump, "crouchDepth", j.crouchDepth);
	readField(jump, "crouchKnee", j.crouchKnee);
	readField(jump, "crouchPitch", j.crouchPitch);
	readField(jump, "launch", j.launch);
	readField(jump, "air", j.air);
	readField(jump, "airPitch", j.airPitch);
	readField(jump, "spread", j.spread);
	readField(jump, "spreadSwing", j.spreadSwing);
	readField(jump, "bounce", j.bounce);
	readField(jump, "hold", j.hold);
	readField(jump, "recover", j.recover);
	readField(jump, "airStretch", j.airStretch);
	readSquash(jump, j.squash);
	readField(death, "duration", motion.death.duration);
	readField(death, "roll", motion.death.roll);
	readField(death, "curl", motion.death.curl);
	return motion;
}
}

std::optional<CreatureDefinition> loadCreatureDefinition(const std::string &relativePath)
{
	sol::state lua;
	const sol::optional<sol::table> table = loadLuaTable(lua, relativePath, "creature", "creature definition");
	if(!table){
		return std::nullopt;
	}
	CreatureDefinition definition;
	readField(table, "name", definition.name);
	readField(table, "length", definition.length);
	definition.spec = readSpec(*table);
	readField(table, "bellyLine", definition.spec.bellyLine);
	definition.motion = readMotion(*table);
	return definition;
}

} // namespace creature
