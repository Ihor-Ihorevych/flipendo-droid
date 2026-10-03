#pragma once

#include "Math/vec.h"
#include "Math/mat.h"
#include "Math/rotator.h"

// Entry points called from the few hp1_re: hooks inside engine/. Everything else HP1-specific
// lives under hp1/. Every hook is already gated by engine->LaunchInfo.IsHarryPotter1().

class UActor;
class Rotator;
class UCanvas;
class UPawn;
class UPlayerPawn;
class CollisionHit;
class UAnimation;
class USkeletalMesh;
class ObjectStream;
class VisibleFrame;
class BBox;
class RenderDevice;
class GameWindow;
struct SceneNode;

namespace HP1
{
	// PackageManager::RegisterFunctions, after upstream registered its natives.
	void RegisterNatives();

	// UAnimation::Load: HP1's packed animation format.
	void LoadAnimation(UAnimation* anim, ObjectStream* stream);

	// UActor::TickAnimation.
	void TickAnimation(UActor* actor, float elapsed);
	// UActor::Tick, at the end: bAnimMove root motion moves the actor (hp1/Anim/HP1Skeletal.cpp).
	void TickRootMotion(UActor* actor);

	// VisibleMesh::DrawSkeletalMesh, after the mesh textures are set up: pose, skin and draw.
	bool DrawSkeletalMesh(VisibleFrame* frame, UActor* actor, UActor* lightLocationActor, USkeletalMesh* mesh, bool translucentPass);

	// UActor::TickMovingBrush: sync HP1's shadowed Mover.PhysAlpha/PhysRate into Actor's and back.
	void MoverPhysicsBegin(UActor* mover);
	void MoverPhysicsEnd(UActor* mover);

	// RenderSubsystem::DrawGame, before presenting: HP1_SHOTS debug screenshots (hp1/HP1Debug.cpp).
	void OnFrameRendered(RenderDevice* device);
	// Engine::Tick, after PlayerCalcView: HP1_CAMERA="x,y,z,pitch,yaw" replaces the view (hp1/HP1Debug.cpp).
	void DebugCamera(vec3& location, Rotator& rotation);

	// Additions the original game doesn't have (hp1/mods/). Engine::Tick, after the console tick.
	void TickMods(float realElapsed);
	// Engine::OnWindowKeyDown, before the key is routed anywhere (EInputKey value).
	void ModsKeyDown(int key);
	// RenderSubsystem::PostRender, after the HUD and the console/menus.
	void PostRenderMods(UCanvas* canvas);

	// UActor::UpdateBspInfo: world space render box of a skeletal mesh actor (culling, BSP placement).
	BBox GetRenderBoundingBox(UActor* actor, USkeletalMesh* mesh);

	// Engine::Tick, after PlayerCalcView: the camera's horizontal FOV for the window's aspect ratio.
	float ViewFovAngle(float fovAngle, int width, int height);
	// RenderSubsystem::DrawGame: the screen flash from HP1's FlashFog (its W replaces FlashScale).
	void ViewFlashParams(UPlayerPawn* player, vec3& flashScale, vec3& flashFog);

	// Widescreen 2D (hp1/HP1Canvas.cpp). RenderSubsystem::ResetCanvas: UI scale for the viewport height.
	float CanvasUIScale(int viewportHeight);
	// RenderSubsystem::ResetCanvas / PostRender: full-width canvas (HUD), or the centred 4:3 area (console/menus).
	void SetCanvasArea(SceneNode& frame, float uiscale, bool menuArea);
	// Engine::OnWindowMouseMove: OS mouse position (pixels) to menu canvas units, for HPConsole's WindowsMouseX/Y.
	void MenuMousePosition(float& x, float& y);
	// Engine::ConsoleCommand "getres": the display's modes, with FEOptionsPage's 1024x768 cap lifted.
	std::string AvailableResolutions(GameWindow* window);

	// Collision (hp1/HP1Collision.cpp): actors with CollideType CT_Box are oriented boxes, not cylinders.
	bool IsBoxCollider(UActor* actor);
	// TraceTester::TraceActor: swept cylinder (height/radius 0 = ray) against the box; returns tmax on a miss.
	double BoxActorTrace(UActor* actor, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, double height, double radius, vec3& outNormal);
	// OverlapTester::CylinderActorOverlap / SphereActorOverlap / IsOverlapping.
	bool BoxActorOverlapCylinder(UActor* actor, const dvec3& center, double height, double radius);
	bool BoxActorOverlapSphere(UActor* actor, const dvec3& center, double radius);
	// CollisionSystem::AddToCollision: half extents of the box's world AABB for the collision hash.
	vec3 BoxCollisionExtents(UActor* actor);

	// UPawn::Tick: APawn::performPhysics' AvgPhysicsTime running average (hp1/HP1Pawn.cpp).
	void PawnPhysicsTime(UPawn* pawn, float elapsed);
	// UPawn::TickRotating / UPlayerPawn::TickRotating: APawn::physicsRotation.
	void PawnPhysicsRotation(UPawn* pawn, float elapsed);
	// ParticleFX (hp1/HP1ParticleFX.cpp). UActor::Tick: AParticleFX::Tick (ages the system, destroys it when done).
	void TickParticleFX(UActor* actor, float elapsed);
	// UActor::Destroy: frees the particle list.
	void ParticleFXDestroyed(UActor* actor);
	// UActor::UpdateBspInfo for DrawType DT_Particles.
	BBox GetParticleBoundingBox(UActor* actor);
	// VisibleActor::DrawTranslucent for DrawType DT_Particles: URender::DrawParticleSystem (hp1/HP1ParticleRender.cpp).
	void DrawParticleSystem(VisibleFrame* frame, UActor* actor);

	// UPawn::TickMoveTo: APawn::moveToward. Returns true when the latent move is done.
	bool PawnMoveToward(UPawn* pawn, const vec3& dest);
	// UPawn::Tick latent polls: MoveToward / StrafeFacing (with AlterDestination), WaitForLanding (LongFall).
	// True when the latent action is done.
	bool PawnPollMoveToward(UPawn* pawn);
	bool PawnPollStrafeFacing(UPawn* pawn);
	bool PawnPollWaitForLanding(UPawn* pawn, float elapsed);
	// UActor::TickWalking / TickFalling on a wall hit: APawn::Mount (ledge grab). True if Pawn.Mount was raised.
	bool PawnMount(UPawn* pawn, const vec3& delta, const CollisionHit& hit);
	// UActor::PreparePawnMovementTick / TickRolling: whether this physics mode fires FellOutOfWorld in zone 0.
	bool PhysicsChecksLeftWorld(UActor* actor);
	// UActor::TickWalking / TickRolling losing the floor: the Falling event, then PHYS_Falling unless the script
	// changed the physics itself. True if the actor is now falling.
	bool StartFalling(UActor* actor);
	// UActor::ShouldAbortJumping (no floor ahead): MayFall, stop at the ledge or start falling. True = stopped.
	bool PawnWalkOffLedge(UPawn* pawn);

	// UActor::TickPhysics for PHYS_Interpolating: an InterpolationManager runs its native performPhysics (moves its
	// Owner along the InterpolationPoint path) instead of upstream's TickInterpolating (hp1/HP1Interpolation.cpp).
	bool IsInterpolationManager(UActor* actor);
	void InterpolationManagerPhysics(UActor* manager, float elapsed);

	// FCoords::OrthoRotation (Core.dll): the rotator of an orthonormal frame (hp1/HP1Interpolation.cpp).
	Rotator OrthoRotation(const vec3& x, const vec3& y, const vec3& z);

	// Bones (hp1/Anim/HP1Skeletal.cpp, hp1/HP1Attach.cpp).
	// USkeletalMesh::GetBoneCoords: world origin and axes of a bone of the actor's current pose.
	bool GetBoneCoords(UActor* actor, USkeletalMesh* mesh, int bone, vec3& origin, vec3& x, vec3& y, vec3& z);
	// The weapon frame (WeaponBoneIndex + WeaponAdjust) of a skeletal mesh, in world space.
	bool SkeletalWeaponFrame(UActor* actor, USkeletalMesh* mesh, vec3& origin, vec3& x, vec3& y, vec3& z);
	// VisibleMesh::DrawMesh, after drawing a pawn: HP1's weapon placement. Sets Pawn.WeaponLoc/WeaponRot and returns
	// the weapon frame -> world matrix if the pawn's mesh has a weapon bone (the weapon's third person mesh goes there).
	bool PawnWeaponFrame(UPawn* pawn, mat4& frameToWorld);
	// Around drawing that weapon: a skeletal weapon mesh (Harry's WandMesh) is placed in the frame instead of at its
	// own Location/Rotation, with this DrawScale (ThirdPersonScale).
	void BeginWeaponDraw(UActor* weapon, const mat4& frameToWorld, float drawScale);
	void EndWeaponDraw();
	// UActor::TickTrailer: AActor::physTrailer (follows the owner, or the owner's bone AnimBone-1).
	void PhysTrailer(UActor* actor);
}
