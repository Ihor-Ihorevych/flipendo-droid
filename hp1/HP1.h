#pragma once

// Entry points called from the few hp1_re: hooks inside engine/. Everything else HP1-specific
// lives under hp1/. Every hook is already gated by engine->LaunchInfo.IsHarryPotter1().

class UActor;
class UAnimation;
class USkeletalMesh;
class ObjectStream;
class VisibleFrame;

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
}
