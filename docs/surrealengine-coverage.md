# SurrealEngine coverage: what HP1 runs on, and whose code it is

SurrealEngine reimplements Unreal Engine 1 to make Unreal and UT99 playable. Its behaviour is written to match those
games, not reversed from Epic's binaries, and KnowWonder changed the engine a lot for HP1. So everything SurrealEngine
does on HP1's path falls into one of three kinds:

| Kind | Meaning | What to do |
|---|---|---|
| **Format** | Only one correct answer (a file format, bytecode semantics, a math function, the platform layer). Wrong shows up as a crash or a load error, not as subtly different behaviour. | Keep SurrealEngine's. |
| **HP1** | HP1's own code, reversed from its DLLs in IDA, in `src/knowwonder/` or `src/hp1/` (`// IDA` tags). | Done; keep it in sync with the IDA notes. |
| **Guess** | SurrealEngine's behaviour, made to look right in Unreal/UT99, and HP1 still runs it. | Unverified: reverse it and port HP1's, or confirm it matches. |

Today's lighting fixes (walls taking the zone behind them, coronas around the camera instead of around Harry) were
both "guess" code; so were the ledge grab and HP1's physics before they were ported. This list exists so the guesses
are found by reading, not by playing until something looks wrong.

Counts as of 2026-10-05: 46 of HP1's script natives are registered from `src/knowwonder/`; 155 still run
SurrealEngine's code (`docs/re/reports/native_audit_hp1.md`, "OK", minus ours). `docs/re/reports/dlls.md` lists the
DLL exports we ported (Engine 199, Render 8, Fire 7, Core 6). Every SurrealEngine file we hook is in
[engine-hooks.md](engine-hooks.md).

## By subsystem

`src/engine/SurrealEngine/` folder, what of it HP1 uses, and whose code runs.

| Subsystem | Kind | HP1 code in place | Still SurrealEngine's |
|---|---|---|---|
| `Package/`, `Packages/Core/` (packages, properties, objects) | Format | struct arrays, ini indexed keys, missing data messages (fixes) | loading itself |
| `VM/` (UnrealScript interpreter) | Format | goto to a missing label, empty-state labels, GotoState re-enables events (fixes) | the interpreter; latent function timing is behaviour (see natives) |
| `GC/`, `Math/`, `Utils/`, `UI/`, `Commandlet/`, `Compiler/`, `Editor/` | Format / unused | crash report text | all |
| `Collision/` | HP1 | BSP line/box/point checks, actor primitives (cylinder, CT_Box), level checks (`KWBspCheck`, `KWCollision`, `KWLevelCheck`) | SurrealEngine's hash still used for actor lookup (extents ours) |
| `Packages/Engine/Actors/UActor_Phys*` (physics) | HP1 | the whole of `performPhysics`: walking, falling, flying, swimming, projectiles, rolling, movers, trailers, interpolation, mounting, auto-jump (`KWPhysics`, `KWMove`, `KWPawn`) | spider physics (unused), `FLIPENDO_SE_PHYSICS=1` fallback |
| Touch, encroachment, MoveActor | HP1 | `KWTouch`, `KWMove` | |
| AI movement natives (MoveTo/MoveToward/StrafeTo/TurnTo polls, WaitForLanding) | HP1 | the polls and rotation (`KWPawn`) | the native entry points |
| AI navigation | HP1 / Guess | `Pawn.FindPath` (553, KnowWonder's station pathing) | `FindPathTo`, `FindPathToward`, `FindRandomDest`, `pointReachable` (few HP1 calls), `actorReachable` is a stub |
| Sight (`LineOfSightTo`, `CanSee`) | HP1 | `APawn::LineOfSightTo` | `PlayerCanSeeMe` (6 calls), `VisibleActors` |
| Animation | HP1 | channels, tweening, root motion, skeletal pose and skinning (`Anim/`) | vertex mesh frame interpolation when drawing |
| Native actor classes | HP1 | ParticleFX, Wind, Gesture, InterpolationManager, IceTexture | other Fire textures (Fire/Water/Wave) |
| Save games | HP1 | `KWSave` | |
| Level load / travel | Guess | startup zones and leaves, the loading screen | `LoadMap` order of events, `ClientTravel`/`ServerTravel`, item travel |
| Actor lifecycle (`Spawn`, `Destroy`, `SetOwner`, `SetCollision`, timers, `Sleep`) | Guess | `Sleep` keeps its time in LatentFloat | `Spawn` (100 calls: placement/encroachment check, event order), `Destroy` (201), `SetTimer` (29) |
| `Light/` light maps | HP1 | ambient fill, per-light lumels, palette, LightSource, LightRadiusInner, mover lights (`KWLightmap`) | LightEffects other than None/NonIncidence/Wavers (~60 lights), the Waver flicker; the empirical 0.5 on lights |
| `Light/` mesh lighting | HP1 / Guess | skeletal meshes (`KWMeshLight`) | **vertex meshes** (`VisibleMesh::DrawMesh`/`DrawLodMesh`: props, non-skeletal models) |
| `Light/` fog (`LightSystem_Fog`, `FogmapBuilder`) | Guess | | volumetric lights and zone fog |
| `Render/` BSP | Guess | the surface's zone (fix), mover polygons one-sided (fix), translucent sort | visibility, portals, mirrors, sky zone, masked/translucent/modulated rules, texture panning |
| `Render/` actors | Guess | shadows (placement), Opacity, particles, coronas, weapon on a bone | **sprites** (`VisibleSprite`), **decals** (`VisibleDecal`, draws the shadows), mesh draw flags, environment maps |
| `Render/RenderCanvas.cpp` (HUD, menus, text) | Guess | widescreen canvas (`HP1Canvas`), view flash | `Canvas.DrawText`/`DrawTile`/`StrLen`/`TextSize`, font rendering |
| `RenderDevice/` | Format | vertex alpha | the GPU backends (replace D3DDrv; its blend modes are a guess worth one check) |
| `Audio/`, `USurrealAudioDevice` (sound) | HP1 / Guess | music changes (`KW::UpdateMusic`), ModifySound/StopSound, `PlaySound` with its hearing test (range, BSP occlusion), Galaxy's volumes and linear falloff ([sound.md](re/engine/sound.md)) | panning (OpenAL's, not Galaxy's pan law), Doppler on ambient sounds, ambient sounds out of range; `GetSoundDuration` |
| Input, console, `GameWindow` | Format / Guess | HP1's `SET Input`, aliases | key handling (should be format; HP1's input classes are script) |
| `Packages/Engine/Network/`, IpDrv, UWeb natives | Unused | | HP1 is single player |

## Script natives still on SurrealEngine's code

Calls in HP1's own scripts (`reference/hp1/ScriptSource/`: HarryPotter, HPBase, HPMenu, HProps, HPParticle,
HPPuzzle, HPDialog, Tut*, Hub*, Hog*, DProps, Engine), most used first. Math, string and object natives
(`FRand`, `IsA`, `VSize`, `Localize`, `Log`, ...) are format and left out.

| Native | Calls | Kind | Why it matters |
|---|---|---|---|
| `Object.GotoState`, `Disable`, `Enable`, `IsInState` | 889, 97, 93, 107 | Format (fixed once) | state changes drive every script; Core.dll's GotoState already compared |
| `Actor.PlaySound` | 232 | HP1 (pan law still OpenAL's) | every sound: how loud, how far, which slot it replaces |
| `Actor.Destroy` | 201 | Guess | Destroyed/touch/base events and their order |
| `Actor.AllActors` (and the other iterators) | 130 | Format | order of actors could matter, likely the level's actor list |
| `Actor.Spawn` | 100 | **Guess** | where a spawned actor ends up when the spot is blocked, which events run |
| `Actor.Sleep` | 89 | Guess (fixed once) | latent timing |
| `ConsoleCommand` (Actor, Console, PlayerPawn) | 58 each | Guess | which commands exist and what they do in HP1 |
| `Actor.FinishInterpolation` | 34 | Guess | cutscene timing |
| `Canvas.DrawText`, `ScriptedTexture.DrawText` | 30, 30 | Guess | text layout in the HUD, menus and the storybook |
| `Actor.SetTimer`, `SetCollision` | 29, 29 | Guess | timer order against the physics (HP1's Tick order is ported) |
| `Object.SaveConfig`, `DynamicLoadObject` | 28, 25 | Format | |
| `Pawn.TurnTo` (entry) | 11 | HP1 poll, SE entry | |
| `PlayerPawn.ClientTravel` | 8 | Guess | level changes |
| `Actor.PlayerCanSeeMe`, `VisibleActors` | 6, 3 | Guess | |
| `Actor.GetSoundDuration` | 4 | Guess | cutscene line timing |
| `Decal.AttachDecal`/`DetachDecal` | 3 | Guess | actor shadows |

## What to reverse next, in order

Ordered by how much of the game it touches and how visible a mismatch is.

1. **Sound: `PlaySound` and the Galaxy device**: done except Galaxy's pan law and ambient Doppler
   ([sound.md](re/engine/sound.md)).
2. **Vertex mesh lighting** (Render.dll's mesh lighting for non-skeletal meshes): props and many decorations.
   The skeletal path is already HP1's (`KWMeshLight`); check whether Render.dll uses the same function for both.
3. **Sprites and decals** (Render.dll `DrawActorSprite`, the decal pass): spell effects drawn as sprites, every actor
   shadow.
4. **`Spawn` and `Destroy`** (Engine.dll `ULevel::SpawnActor`, `ULevel::DestroyActor`): placement and event order.
5. **BSP drawing rules** (Render.dll `DrawFrame` and the surface setup): masked/translucent/modulated, two-sided,
   texture panning and scaling, sky zone, mirrors, fog. Today's two lighting bugs came from this area.
6. **Canvas text and tiles** (Engine.dll `UCanvas`): HUD, menus, storybook.
7. **Level load and travel** (`UGameEngine::LoadMap`, `Browse`, `ClientTravel`): event order on load and between
   levels.
8. **The remaining light effects** (~60 lights) and the Waver flicker; the factor 2 behind the lights' 0.5.
9. The rest of the "guess" rows above, as they come up.

Each item: find the original in IDA (`../ida/hp1/decomp/` first), port it into `src/knowwonder/` with `// IDA` tags,
compare with the original's frames or sounds (`tools/orig_shots.ps1`, [debug-tools.md](debug-tools.md)), then move
its row here from "Guess" to "HP1".

## Keeping this current

When a port lands, update its row. To recount the natives: `python tools/native_audit.py hp1`, then compare its "OK"
list with the names registered in `src/knowwonder/` and `src/hp1/` (`RegisterVMNativeFunc_*("Class", "Name", ...)`).
