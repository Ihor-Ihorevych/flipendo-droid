#pragma once

// Entry points called from the few hp1_re: hooks inside engine/. Everything else HP1-specific
// lives under hp1/. Every hook is already gated by engine->LaunchInfo.IsHarryPotter1().

class UActor;
class UAnimation;
class USkeletalMesh;
class ObjectStream;
class VisibleFrame;
class BBox;
class RenderDevice;

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
}
