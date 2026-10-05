#pragma once

#include "Math/vec.h"
#include "Math/mat.h"
#include "Math/rotator.h"
#include <string>
#include <memory>
#include <vector>

// KnowWonder's engine (src/knowwonder/): what Flipendo reimplements of the modified Engine.dll / Fire.dll that the Harry Potter
// games share. These are the entry points called from our hooks inside src/engine/, each gated by
// engine->LaunchInfo.IsKnowWonder() (HP1 for now; HP2 joins once its differences are handled, docs/re/reports/hp2_compare.md).
// Game-specific code (HP1's mods and menu canvas) is in src/hp1/HP1.h.

class UObject;
class UActor;
class Rotator;
class UCanvas;
class UPawn;
class UZoneInfo;
class UPlayerPawn;
class AudioDevice;
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
struct GouraudVertex;

class Image;

namespace KW
{
	// Engine::OpenWindow: the game's own icon from the player's folder (System/<exe>.ico, src/knowwonder/KWIcon.cpp); empty if missing.
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
	// UActor::Touch: AActor::BeginTouch, Actor (the one moving) touches Other (src/knowwonder/KWTouch.cpp).
	void BeginTouch(UActor* actor, UActor* other);

	// UActor::Tick, at the end: bAnimMove root motion moves the actor (src/knowwonder/Anim/KWSkeletal.cpp).
	void TickRootMotion(UActor* actor);

	// VisibleMesh::DrawSkeletalMesh, after the mesh textures are set up: pose, skin and draw.
	bool DrawSkeletalMesh(VisibleFrame* frame, UActor* actor, UActor* lightLocationActor, USkeletalMesh* mesh, bool translucentPass);

	// UActor::TickPhysics: true when KnowWonder's own physics runs (not with FLIPENDO_SE_PHYSICS=1, which keeps
	// SurrealEngine's for comparison). Then KW::PhysicsTick replaces the whole physics step.
	bool UseKWPhysics();
	void SetStartupZones(size_t loadedCount);
	// UActor::CheckLocation (spawning): ULevel::SpawnActor's FindSpot(cylinder, Location, no actors, check first)
	bool SpawnFindSpot(float radius, float height, vec3& location);
	void PhysicsTick(UActor* actor, float elapsed);

	// UActor::TickMovingBrush: KnowWonder's physMovingBrush (Mover's own PhysAlpha/PhysRate, falling, world collision).
	void PhysMovingBrush(UActor* mover, float deltaTime);

	// RenderSubsystem::DrawGame, before presenting: HP1_SHOTS debug screenshots (src/knowwonder/KWDebug.cpp).
	void OnFrameRendered(RenderDevice* device);
	// Engine::Tick, after PlayerCalcView: HP1_CAMERA="x,y,z,pitch,yaw" replaces the view (src/knowwonder/KWDebug.cpp).
	void DebugCamera(vec3& location, Rotator& rotation);
	// Engine::OpenWindow: HP1_BACKGROUND=1 shows the window windowed, behind the others and without taking focus
	// (src/knowwonder/KWDebug.cpp). Returns false (window shown as usual) when it isn't set.
	bool ShowWindowInBackground(GameWindow* window, int width, int height);

	// Engine::ConsoleCommand "open": FESlotPage loads a slot with "open saveN.usa". Returns "?load=N" when the map names
	// an existing save file in the Save folder, else empty (src/knowwonder/KWSave.cpp).
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
	// Actor.GetWorldCollisionBox for a skeletal mesh: the mesh's bounding box through GetMeshCoords (src/knowwonder/Anim).
	BBox GetSkeletalCollisionBox(UActor* actor, USkeletalMesh* mesh);

	// LightmapBuilder (BSP light maps, src/knowwonder/KWLightmap.cpp). SetAmbientLight: the zone ambient a light map starts with.
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
	// Engine::LoadMap / LoadFromSaveFile: HP1's loading screen. LoadMapFadeOut at the start (fade to black over
	// Console.FadeoutTime), LoadMapLevelInfo once the new LevelInfo is loaded (Console.DrawLevelInfo) (src/knowwonder/KWView.cpp).
	void LoadMapFadeOut();
	void LoadMapLevelInfo(ULevelInfo* levelInfo);

	// USurrealAudioDevice::Update, in place of UpdateMusic (src/knowwonder/KWSound.cpp): Galaxy's music update (song
	// changes with fades, looping unless bDontLoopSong, bSongFinished). Returns the music volume (0-1) to set.
	float UpdateMusic(AudioDevice* device, UPlayerPawn* player, uint8_t musicVolume, int latency);

	// Collision (src/knowwonder/KWCollision.cpp): actors with CollideType CT_Box are oriented boxes, not cylinders.
	bool IsBoxCollider(UActor* actor);
	// FCollisionHash::GetActorExtent: the world box of the actor's collision primitive (CollisionSystem::AddToCollision).
	BBox ActorWorldCollisionBox(UActor* actor);
	// UActor::TryMove: whether the moving actor is stopped by (and bumps) another one, or touches it.
	bool IsBlockedBy(UActor* self, UActor* other);
	// TraceTester::TraceActor: swept cylinder (height/radius 0 = ray) against the box; returns tmax on a miss.
	double BoxActorTrace(UActor* actor, const dvec3& origin, double tmin, const dvec3& dirNormalized, double tmax, double height, double radius, vec3& outNormal);
	// OverlapTester::CylinderActorOverlap / SphereActorOverlap / IsOverlapping.
	bool BoxActorOverlapCylinder(UActor* actor, const dvec3& center, double height, double radius);
	bool BoxActorOverlapSphere(UActor* actor, const dvec3& center, double radius);

	// UPawn::Tick: APawn::performPhysics' AvgPhysicsTime running average (src/knowwonder/KWPawn.cpp).
	void PawnPhysicsTime(UPawn* pawn, float elapsed);
	// UPawn::TickRotating / UPlayerPawn::TickRotating: APawn::physicsRotation.
	void PawnPhysicsRotation(UPawn* pawn, float elapsed);
	// UActor::Tick, first: the Tick overrides of HP1's native actor classes. AParticleFX::Tick ages the system and
	// destroys it when done (src/knowwonder/KWParticleFX.cpp); AWind::Tick moves the wind's fluctuation (src/knowwonder/KWWind.cpp).
	void TickNativeActor(UActor* actor, float elapsed);
	// UActor::Destroy: frees the particle list.
	void ParticleFXDestroyed(UActor* actor);
	// UActor::UpdateBspInfo for DrawType DT_Particles.
	BBox GetParticleBoundingBox(UActor* actor);
	// VisibleActor::DrawTranslucent for DrawType DT_Particles: URender::DrawParticleSystem (src/knowwonder/KWParticleRender.cpp).
	void DrawParticleSystem(VisibleFrame* frame, UActor* actor);
	// VisibleFrame::Process, before the BSP walk: Shadow.Update for every drawn actor whose shadow area is on screen
	// (URender::SetupDynamics, src/knowwonder/KWActorRender.cpp).
	void UpdateActorShadows(VisibleFrame* frame);
	// Mesh drawing (VisibleMesh, KW::DrawSkeletalMesh): Actor.Opacity < 1 alpha blends the mesh. MeshOpacity once per
	// actor, ApplyOpacityFlags on each face's flags before the translucent-pass test, ApplyOpacityVertices before drawing.
	float MeshOpacity(UActor* actor);
	void ApplyOpacityFlags(float opacity, uint32_t& polyFlags);
	void ApplyOpacityVertices(float opacity, GouraudVertex* vertices, int count);

	// UPawn::TickMoveTo: APawn::moveToward. Returns true when the latent move is done.
	bool PawnMoveToward(UPawn* pawn, const vec3& dest);
	// UPawn::Tick latent polls: MoveToward / StrafeFacing (with AlterDestination), WaitForLanding (LongFall).
	// True when the latent action is done.
	bool PawnPollMoveToward(UPawn* pawn);
	bool PawnPollStrafeFacing(UPawn* pawn);
	// UPawn::TickRotateTo: APawn::rotateToward (DesiredRotation; true when facing the focal point).
	bool PawnRotateToward(UPawn* pawn, const vec3& focalPoint);
	// UPawn::TurnTo / TurnToward and their latent polls: one step of the turn (flying acceleration + rotateToward).
	bool PawnTurnStep(UPawn* pawn, const vec3& focus);
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
	// Owner along the InterpolationPoint path) instead of upstream's TickInterpolating (src/knowwonder/KWInterpolation.cpp).
	bool IsInterpolationManager(UActor* actor);
	void InterpolationManagerPhysics(UActor* manager, float elapsed);

	// FCoords::OrthoRotation (Core.dll): the rotator of an orthonormal frame (src/knowwonder/KWInterpolation.cpp).
	Rotator OrthoRotation(const vec3& x, const vec3& y, const vec3& z);

	// Bones (src/knowwonder/Anim/KWSkeletal.cpp, src/knowwonder/KWAttach.cpp).
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
