#pragma once

#include "Math/vec.h"

// Entry points called from the few hp1_re: hooks inside engine/. Everything else HP1-specific
// lives under hp1/. Every hook is already gated by engine->LaunchInfo.IsHarryPotter1().

class UActor;
class UPawn;
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

	// VisibleMesh::DrawSkeletalMesh, after the mesh textures are set up: pose, skin and draw.
	bool DrawSkeletalMesh(VisibleFrame* frame, UActor* actor, UActor* lightLocationActor, USkeletalMesh* mesh, bool translucentPass);

	// UActor::TickMovingBrush: sync HP1's shadowed Mover.PhysAlpha/PhysRate into Actor's and back.
	void MoverPhysicsBegin(UActor* mover);
	void MoverPhysicsEnd(UActor* mover);

	// RenderSubsystem::DrawGame, before presenting: HP1_SHOTS debug screenshots (hp1/HP1Debug.cpp).
	void OnFrameRendered(RenderDevice* device);

	// UActor::UpdateBspInfo: world space render box of a skeletal mesh actor (culling, BSP placement).
	BBox GetRenderBoundingBox(UActor* actor, USkeletalMesh* mesh);

	// Engine::Tick, after PlayerCalcView: the camera's horizontal FOV for the window's aspect ratio.
	float ViewFovAngle(float fovAngle, int width, int height);

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
	// UPawn::TickMoveTo: APawn::moveToward. Returns true when the latent move is done.
	bool PawnMoveToward(UPawn* pawn, const vec3& dest);
}
