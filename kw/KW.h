#pragma once

#include "Math/vec.h"
#include "Math/mat.h"
#include "Math/rotator.h"
#include <string>
#include <memory>
#include <vector>

// KnowWonder's engine (kw/): what Flipendo reimplements of the modified Engine.dll / Fire.dll that the Harry Potter
// games share. These are the entry points called from our hooks inside engine/, each gated by
// engine->LaunchInfo.IsKnowWonder() (HP1 for now; HP2 joins once its differences are handled, docs/re/reports/hp2_compare.md).
// Game-specific code (HP1's mods and menu canvas) is in hp1/HP1.h.

class UObject;
class UActor;
class Rotator;
class UCanvas;
class UPawn;
class UZoneInfo;
class UPlayerPawn;
class UMusic;
class AudioSource;
class ULevelInfo;
class CollisionHit;
class UAnimation;
class USkeletalMesh;
class UIceTexture;
class ObjectStream;
class VisibleFrame;
class BBox;
class RenderDevice;
class GameWindow;
struct SceneNode;

class Image;

namespace KW
{
	// Engine::OpenWindow: the game's own icon from the player's folder (System/<exe>.ico, kw/KWIcon.cpp); empty if missing.
	std::vector<std::shared_ptr<Image>> GameIcons(const std::string& gameRootFolder, const std::string& exeName);

	// Script property helpers: an object reference / bool property by name (nullptr / false if absent).
	UObject* ObjectProperty(UObject* obj, const char* name);
	bool BoolProperty(UObject* obj, const char* name);

	// PackageManager::RegisterFunctions, after upstream registered its natives.
	void RegisterNatives();

	// UAnimation::Load: HP1's packed animation format.
	void LoadAnimation(UAnimation* anim, ObjectStream* stream);

	// UActor::TickAnimation.
	void TickAnimation(UActor* actor, float elapsed);
	// UActor::Touch: AActor::BeginTouch, Actor (the one moving) touches Other (kw/KWTouch.cpp).
	void BeginTouch(UActor* actor, UActor* other);

	// UActor::Tick, at the end: bAnimMove root motion moves the actor (kw/Anim/KWSkeletal.cpp).
	void TickRootMotion(UActor* actor);

	// VisibleMesh::DrawSkeletalMesh, after the mesh textures are set up: pose, skin and draw.
	bool DrawSkeletalMesh(VisibleFrame* frame, UActor* actor, UActor* lightLocationActor, USkeletalMesh* mesh, bool translucentPass);

	// UActor::TickMovingBrush: sync HP1's shadowed Mover.PhysAlpha/PhysRate into Actor's and back.
	void MoverPhysicsBegin(UActor* mover);
	void MoverPhysicsEnd(UActor* mover);

	// RenderSubsystem::DrawGame, before presenting: HP1_SHOTS debug screenshots (kw/KWDebug.cpp).
	void OnFrameRendered(RenderDevice* device);
	// Engine::Tick, after PlayerCalcView: HP1_CAMERA="x,y,z,pitch,yaw" replaces the view (kw/KWDebug.cpp).
	void DebugCamera(vec3& location, Rotator& rotation);
	// Engine::OpenWindow: HP1_BACKGROUND=1 shows the window windowed, behind the others and without taking focus
	// (kw/KWDebug.cpp). Returns false (window shown as usual) when it isn't set.
	bool ShowWindowInBackground(GameWindow* window, int width, int height);

	// Engine::ConsoleCommand "open": FESlotPage loads a slot with "open saveN.usa". Returns "?load=N" when the map names
	// an existing save file in the Save folder, else empty (kw/KWSave.cpp).
	// Engine::ConsoleCommand "SaveGame N": UGameEngine::SaveGame (LevelAction, the progress frame, then the save).
	void SaveGame(int slot, const std::string& description);
	std::string SaveGameLoadURL(const std::string& map);
	// Engine::ConsoleCommand, before the exec functions: console commands of KnowWonder's UViewport::Exec that
	// SurrealEngine doesn't know ("Snap"). Returns true when handled.
	bool ViewportCommand(const Array<std::string>& args);
	// Engine, right after a save is loaded: every actor's BSP leaf found again from its location.
	void SaveGameLoaded();
	// Engine::LoadMap, after the LevelInfo is found: UGameEngine::LoadMap sets an empty LevelEnterText to URL.Map
	// ("Lev_Tut1.unr"); HPConsole.doLevelSave names the save slot (and its thumbnail) after it.
	void LevelInfoLoaded(ULevelInfo* levelInfo, const std::string& urlMap);

	// UActor::UpdateBspInfo: world space render box of a skeletal mesh actor (culling, BSP placement).
	BBox GetRenderBoundingBox(UActor* actor, USkeletalMesh* mesh);
	// Actor.GetWorldCollisionBox for a skeletal mesh: the mesh's bounding box through GetMeshCoords (kw/Anim).
	BBox GetSkeletalCollisionBox(UActor* actor, USkeletalMesh* mesh);

	// LightmapBuilder (BSP light maps, kw/KWLightmap.cpp). SetAmbientLight: the zone ambient a light map starts with.
	vec3 LightmapAmbient(UZoneInfo* zone);
	// AddStaticLights/AddDynamicLights, instead of LightEffect::Run: a light's reach per lumel (0..1, shadow included)
	// with LightSource and LightRadiusInner. Returns false for the light effects left to SurrealEngine.
	bool LightmapIllumination(UActor* light, int size, const vec3* locations, const vec3& base, const vec3& normal, const float* shadowmap, float* result);
	// AddLightContribution: adds (bDarkLight: subtracts) the light's colour, clamped like HP1's 7-bit light maps.
	void AddLightmapLight(UActor* light, const float* illumination, vec3* lightcolors, int size);
	// GetMoverLightmap for a bDynamicLightMover: the lights a surface (world space point and normal) is lit by, the
	// lights permeating the mover's BSP leaf in front of the surface. Appends to `lights`.
	void DynamicMoverLights(UActor* mover, const vec3& point, const vec3& normal, std::vector<UActor*>& lights);

	// Engine::Tick, after PlayerCalcView: the camera's horizontal FOV for the window's aspect ratio.
	float ViewFovAngle(float fovAngle, int width, int height);
	// RenderSubsystem::DrawGame: the screen flash from HP1's FlashFog (its W replaces FlashScale).
	void ViewFlashParams(UPlayerPawn* player, vec3& flashScale, vec3& flashFog);

	// USurrealAudioDevice::UpdateMusic (kw/KWSound.cpp): MusicSource wraps a new song's stream (loops unless
	// bDontLoopSong, silence after the end); TickMusic, every update, sets bSongFinished and clears an ended Song.
	std::unique_ptr<AudioSource> MusicSource(UPlayerPawn* player, UMusic* song, std::unique_ptr<AudioSource> source);
	void TickMusic(UPlayerPawn* player, UMusic* currentSong);

	// Collision (kw/KWCollision.cpp): actors with CollideType CT_Box are oriented boxes, not cylinders.
	bool IsBoxCollider(UActor* actor);
	// UActor::TryMove: whether the moving actor is stopped by (and bumps) another one, or touches it.
	bool IsBlockedBy(UActor* self, UActor* other);
	// TraceTester::TraceActor: swept cylinder (height/radius 0 = ray) against the box; returns tmax on a miss.
	double BoxActorTrace(UActor* actor, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, double height, double radius, vec3& outNormal);
	// OverlapTester::CylinderActorOverlap / SphereActorOverlap / IsOverlapping.
	bool BoxActorOverlapCylinder(UActor* actor, const dvec3& center, double height, double radius);
	bool BoxActorOverlapSphere(UActor* actor, const dvec3& center, double radius);
	// CollisionSystem::AddToCollision: half extents of the box's world AABB for the collision hash.
	vec3 BoxCollisionExtents(UActor* actor);

	// UPawn::Tick: APawn::performPhysics' AvgPhysicsTime running average (kw/KWPawn.cpp).
	void PawnPhysicsTime(UPawn* pawn, float elapsed);
	// UPawn::TickRotating / UPlayerPawn::TickRotating: APawn::physicsRotation.
	void PawnPhysicsRotation(UPawn* pawn, float elapsed);
	// UActor::Tick, first: the Tick overrides of HP1's native actor classes. AParticleFX::Tick ages the system and
	// destroys it when done (kw/KWParticleFX.cpp); AWind::Tick moves the wind's fluctuation (kw/KWWind.cpp).
	void TickNativeActor(UActor* actor, float elapsed);
	// UActor::Destroy: frees the particle list.
	void ParticleFXDestroyed(UActor* actor);
	// UActor::UpdateBspInfo for DrawType DT_Particles.
	BBox GetParticleBoundingBox(UActor* actor);
	// VisibleActor::DrawTranslucent for DrawType DT_Particles: URender::DrawParticleSystem (kw/KWParticleRender.cpp).
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
	// Owner along the InterpolationPoint path) instead of upstream's TickInterpolating (kw/KWInterpolation.cpp).
	bool IsInterpolationManager(UActor* actor);
	void InterpolationManagerPhysics(UActor* manager, float elapsed);

	// FCoords::OrthoRotation (Core.dll): the rotator of an orthonormal frame (kw/KWInterpolation.cpp).
	Rotator OrthoRotation(const vec3& x, const vec3& y, const vec3& z);

	// Bones (kw/Anim/KWSkeletal.cpp, kw/KWAttach.cpp).
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

	// UIceTexture::UpdateFrame: Fire.dll's IceTexture (refraction of SourceTexture through GlassTexture, panning).
	void UpdateIceTexture(UIceTexture* ice, float frameTime);
}
