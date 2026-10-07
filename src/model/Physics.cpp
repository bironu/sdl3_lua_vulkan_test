#include "model/Physics.h"
#include <SDL3/SDL_log.h>
#include <btBulletDynamicsCommon.h>
#include <algorithm>
#include <vector>

namespace model
{

namespace
{

// オイラー角(R = Ry * Rx * Rz)→回転
btQuaternion eulerToQuaternion(const float e[3])
{
	const btQuaternion qy(btVector3(0, 1, 0), e[1]);
	const btQuaternion qx(btVector3(1, 0, 0), e[0]);
	const btQuaternion qz(btVector3(0, 0, 1), e[2]);
	return qy * qx * qz;
}

btTransform toBtTransform(const Mat34 &m)
{
	btTransform t;
	t.setBasis(btMatrix3x3(m.r[0][0], m.r[0][1], m.r[0][2], m.r[1][0], m.r[1][1], m.r[1][2], m.r[2][0], m.r[2][1], m.r[2][2]));
	t.setOrigin(btVector3(m.t[0], m.t[1], m.t[2]));
	return t;
}

Mat34 fromBtTransform(const btTransform &t)
{
	Mat34 m;
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			m.r[i][j] = t.getBasis()[i][j];
		}
	}
	m.t[0] = t.getOrigin().x();
	m.t[1] = t.getOrigin().y();
	m.t[2] = t.getOrigin().z();
	return m;
}

} // namespace

struct Physics::Impl
{
	struct Body
	{
		std::unique_ptr<btCollisionShape> shape;
		std::unique_ptr<btDefaultMotionState> motionState;
		std::unique_ptr<btRigidBody> body;
		int bone = -1;
		ModelRigidBody::Type type = ModelRigidBody::Type::BoneFollow;
		btTransform offset;        // ボーンのグローバル姿勢から見た、剛体の位置(休止ポーズで固定)
		btTransform offsetInverse;
		btTransform initial;       // 休止ポーズでの剛体のワールド姿勢
	};

	std::unique_ptr<btDefaultCollisionConfiguration> configuration;
	std::unique_ptr<btCollisionDispatcher> dispatcher;
	std::unique_ptr<btDbvtBroadphase> broadphase;
	std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
	std::unique_ptr<btDiscreteDynamicsWorld> world;
	std::unique_ptr<btStaticPlaneShape> groundShape;
	std::unique_ptr<btDefaultMotionState> groundMotion;
	std::unique_ptr<btRigidBody> ground;
	std::vector<Body> bodies;
	std::vector<std::unique_ptr<btGeneric6DofSpringConstraint>> joints;
	std::vector<int> outBones;      // step()で書き戻すボーン(物理演算で動く剛体のもの)
	std::vector<Mat34> outGlobals;
	float accumulator = 0.0f;
	// 世界を作り直す(reset)ための、元の剛体とジョイント
	std::vector<ModelRigidBody> sourceBodies;
	std::vector<ModelJoint> sourceJoints;

	~Impl()
	{
		for(auto &j : joints){ world->removeConstraint(j.get()); }
		for(auto &b : bodies){ world->removeRigidBody(b.body.get()); }
		if(ground){ world->removeRigidBody(ground.get()); }
	}

	// ボーンに追従する剛体を、ボーンの現在のグローバル姿勢へ動かす
	void followBones(const Skeleton &skeleton)
	{
		for(auto &b : bodies){
			if(b.type != ModelRigidBody::Type::BoneFollow){
				continue;
			}
			const btTransform world = b.bone >= 0 ? toBtTransform(skeleton.globalTransform(b.bone)) * b.offset : b.initial;
			b.motionState->setWorldTransform(world);
			b.body->setWorldTransform(world);
		}
	}
};

Physics::Physics() = default;
Physics::~Physics() = default;

// 物理の世界(剛体・ジョイント)を作る。剛体は休止ポーズの位置に置く
static void buildWorld(Physics::Impl &impl, const std::vector<ModelRigidBody> &rigidBodies, const std::vector<ModelJoint> &joints, const Skeleton &skeleton)
{
	impl.configuration = std::make_unique<btDefaultCollisionConfiguration>();
	impl.dispatcher = std::make_unique<btCollisionDispatcher>(impl.configuration.get());
	impl.broadphase = std::make_unique<btDbvtBroadphase>();
	impl.solver = std::make_unique<btSequentialImpulseConstraintSolver>();
	impl.world = std::make_unique<btDiscreteDynamicsWorld>(impl.dispatcher.get(), impl.broadphase.get(), impl.solver.get(), impl.configuration.get());
	impl.world->setGravity(btVector3(0, -9.8f * 10.0f, 0)); // MMDの単位(1 = 約8cm)に合わせた重力

	// 床(モデルの原点の高さ)。どの剛体と衝突するかは、剛体側の衝突しないグループで決まる(グループ15)
	impl.groundShape = std::make_unique<btStaticPlaneShape>(btVector3(0, 1, 0), 0.0f);
	impl.groundMotion = std::make_unique<btDefaultMotionState>(btTransform::getIdentity());
	impl.ground = std::make_unique<btRigidBody>(btRigidBody::btRigidBodyConstructionInfo(0.0f, impl.groundMotion.get(), impl.groundShape.get()));
	impl.world->addRigidBody(impl.ground.get(), 1 << 15, 0xFFFF);

	impl.bodies.resize(rigidBodies.size());
	for(size_t i = 0; i < rigidBodies.size(); ++i){
		const ModelRigidBody &src = rigidBodies[i];
		Physics::Impl::Body &b = impl.bodies[i];
		b.bone = src.bone >= 0 && src.bone < static_cast<int>(skeleton.boneCount()) ? src.bone : -1;
		b.type = src.type;
		switch(src.shape){
		case ModelRigidBody::Shape::Sphere:
			b.shape = std::make_unique<btSphereShape>(src.size[0]);
			break;
		case ModelRigidBody::Shape::Box:
			b.shape = std::make_unique<btBoxShape>(btVector3(src.size[0], src.size[1], src.size[2]));
			break;
		case ModelRigidBody::Shape::Capsule:
			b.shape = std::make_unique<btCapsuleShape>(src.size[0], src.size[1]);
			break;
		}
		b.initial = btTransform(eulerToQuaternion(src.rotation), btVector3(src.position[0], src.position[1], src.position[2]));
		// 休止ポーズのボーンのグローバル姿勢は、回転なし・位置=ボーンの位置
		btTransform boneRest = btTransform::getIdentity();
		if(b.bone >= 0){
			const ModelBone &bone = skeleton.bone(b.bone);
			boneRest.setOrigin(btVector3(bone.position[0], bone.position[1], bone.position[2]));
		}
		b.offset = boneRest.inverse() * b.initial;
		b.offsetInverse = b.offset.inverse();

		const bool dynamic = src.type != ModelRigidBody::Type::BoneFollow && src.mass > 0.0f;
		btVector3 inertia(0, 0, 0);
		const float mass = dynamic ? src.mass : 0.0f;
		if(dynamic){
			b.shape->calculateLocalInertia(mass, inertia);
		}
		b.motionState = std::make_unique<btDefaultMotionState>(b.initial);
		btRigidBody::btRigidBodyConstructionInfo info(mass, b.motionState.get(), b.shape.get(), inertia);
		info.m_linearDamping = src.linearDamping;
		info.m_angularDamping = src.angularDamping;
		info.m_restitution = src.restitution;
		info.m_friction = src.friction;
		info.m_additionalDamping = true;
		b.body = std::make_unique<btRigidBody>(info);
		if(!dynamic){
			b.body->setCollisionFlags(b.body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
		}
		b.body->setActivationState(DISABLE_DEACTIVATION);
		const int group = std::clamp<int>(src.group, 0, 15);
		const int mask = ~static_cast<int>(src.nonCollisionMask) & 0xFFFF;
		impl.world->addRigidBody(b.body.get(), 1 << group, mask);

		if(dynamic && b.bone >= 0){
			impl.outBones.push_back(b.bone);
		}
	}

	for(const auto &src : joints){
		if(src.rigidA < 0 || src.rigidB < 0 || src.rigidA >= static_cast<int>(impl.bodies.size()) || src.rigidB >= static_cast<int>(impl.bodies.size())){
			continue;
		}
		const btTransform jointWorld(eulerToQuaternion(src.rotation), btVector3(src.position[0], src.position[1], src.position[2]));
		Physics::Impl::Body &a = impl.bodies[src.rigidA];
		Physics::Impl::Body &b = impl.bodies[src.rigidB];
		auto joint = std::make_unique<btGeneric6DofSpringConstraint>(*a.body, *b.body, a.initial.inverse() * jointWorld, b.initial.inverse() * jointWorld, true);
		joint->setLinearLowerLimit(btVector3(src.moveLimitMin[0], src.moveLimitMin[1], src.moveLimitMin[2]));
		joint->setLinearUpperLimit(btVector3(src.moveLimitMax[0], src.moveLimitMax[1], src.moveLimitMax[2]));
		joint->setAngularLowerLimit(btVector3(src.rotationLimitMin[0], src.rotationLimitMin[1], src.rotationLimitMin[2]));
		joint->setAngularUpperLimit(btVector3(src.rotationLimitMax[0], src.rotationLimitMax[1], src.rotationLimitMax[2]));
		for(int k = 0; k < 3; ++k){
			if(src.springMove[k] != 0.0f){
				joint->enableSpring(k, true);
				joint->setStiffness(k, src.springMove[k]);
			}
			if(src.springRotation[k] != 0.0f){
				joint->enableSpring(3 + k, true);
				joint->setStiffness(3 + k, src.springRotation[k]);
			}
		}
		joint->setEquilibriumPoint();
		impl.world->addConstraint(joint.get(), true); // つないだ剛体どうしは衝突させない
		impl.joints.push_back(std::move(joint));
	}
	impl.outGlobals.resize(impl.outBones.size());
}

std::unique_ptr<Physics> Physics::create(const ModelData &data, const Skeleton &skeleton)
{
	if(data.rigidBodies.empty()){
		return nullptr;
	}
	std::unique_ptr<Physics> result(new Physics());
	result->impl_ = std::make_unique<Impl>();
	result->impl_->sourceBodies = data.rigidBodies;
	result->impl_->sourceJoints = data.joints;
	buildWorld(*result->impl_, data.rigidBodies, data.joints, skeleton);
	SDL_Log("Physics: %zu rigid bodies (%zu simulated), %zu joints", result->impl_->bodies.size(), result->impl_->outBones.size(), result->impl_->joints.size());
	return result;
}

void Physics::reset(const Skeleton &skeleton)
{
	// 剛体の位置を書き換えるだけでは、前の姿勢で溜まった接触・拘束の情報が残って、戻した直後に暴れる(スカートが飛ぶ)。
	// 世界ごと作り直して、溜まった状態を捨てる
	{
		std::vector<ModelRigidBody> bodies = std::move(impl_->sourceBodies);
		std::vector<ModelJoint> joints = std::move(impl_->sourceJoints);
		impl_ = std::make_unique<Impl>();
		impl_->sourceBodies = bodies;
		impl_->sourceJoints = joints;
		buildWorld(*impl_, bodies, joints, skeleton);
	}
	Impl &impl = *impl_;
	impl.followBones(skeleton);
	for(auto &b : impl.bodies){
		if(b.type == ModelRigidBody::Type::BoneFollow){
			continue;
		}
		// 動く剛体は、ボーンのグローバル姿勢から決まる位置へ戻す(関連するボーンが無ければ休止ポーズの位置)
		const btTransform world = b.bone >= 0 ? toBtTransform(skeleton.globalTransform(b.bone)) * b.offset : b.initial;
		b.body->setWorldTransform(world);
		b.motionState->setWorldTransform(world);
		b.body->setLinearVelocity(btVector3(0, 0, 0));
		b.body->setAngularVelocity(btVector3(0, 0, 0));
		b.body->clearForces();
	}
	impl.world->clearForces();
}

void Physics::step(Skeleton &skeleton, float dt)
{
	Impl &impl = *impl_;
	impl.followBones(skeleton);
	// 固定の刻みで進める(速いフレームレートでも遅いフレームレートでも、揺れ方が変わらないように)
	constexpr float kFixedStep = 1.0f / 60.0f;
	impl.accumulator = std::min(impl.accumulator + dt, kFixedStep * 5.0f); // 溜めすぎない(止まっていた後に暴れない)
	while(impl.accumulator >= kFixedStep){
		impl.world->stepSimulation(kFixedStep, 0, kFixedStep);
		impl.accumulator -= kFixedStep;
	}

	// 物理演算で動く剛体の姿勢から、ボーンのグローバル姿勢を求める
	size_t out = 0;
	for(auto &b : impl.bodies){
		if(b.type == ModelRigidBody::Type::BoneFollow || b.bone < 0 || !b.body || b.body->isStaticOrKinematicObject()){
			continue;
		}
		btTransform world;
		b.motionState->getWorldTransform(world);
		btTransform boneWorld = world * b.offsetInverse;
		if(b.type == ModelRigidBody::Type::PhysicsAligned){
			// 回転は物理演算、位置はアニメーションのボーンの位置のまま
			const Mat34 &anim = skeleton.globalTransform(b.bone);
			boneWorld.setOrigin(btVector3(anim.t[0], anim.t[1], anim.t[2]));
		}
		if(out < impl.outGlobals.size()){
			impl.outGlobals[out++] = fromBtTransform(boneWorld);
		}
	}
	// outBonesとoutGlobalsは同じ順(剛体の並び順で、動くものだけ)
	skeleton.applyPhysics(impl.outBones, impl.outGlobals);
}

} // namespace model
