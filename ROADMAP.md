# Roadmap

Where the HP1 port stands and what's next. Update this when a step lands. Details of what was reversed
live in `docs/re/`. The native-level checklist is `docs/native_audit.md` (`python tools/native_audit.py`).

Legend: [x] done · [~] partly done / in progress · [ ] not started

## 0. Groundwork
- [x] SurrealEngine imported as a git subtree in `engine/`, `tools/update_engine.sh` for upstream pulls
- [x] HP1 retail exe detection (SafeDisc + No-CD hashes), `--autolaunch`, `--logfile`
- [x] HP1 code lives in `hp1/`, engine changes are small `hp1_re:` hooks (see "Code layout" in CLAUDE.md)

## 1. Characters move (skeletal animation) ← current
- [x] `UAnimation` HP1 format loader (`hp1/Anim/HP1Animation.cpp`)
- [x] Anim state natives: PlayAnim, LoopAnim, TweenAnim, IsAnimating, FinishAnim, CreateAnimChannel,
      HasAnim, GetAnimGroup, LinkSkelAnim, BoneNumber, BoneName
- [x] Anim tick: TweenAlpha blend, notifies, AnimEnd, aux channels
- [ ] **Skeletal pose + skinning so characters render** (`USkeletalMesh::GetFrame` / `ApplyAnim`). Until this
      is done character models are invisible.
- [ ] Channel blending in the pose (AuxAnims per bone subtree, AT_Combine)
- [ ] Root motion (`bAnimMove`: GetRootMovement / AdjustRootMovement / AnimCycleMovement)
- [ ] BonePos, GetBoneCoords, weapon/wand attachment (WeaponBoneIndex, WeaponAdjust), AttachToOwner
- [ ] GetRenderExtent, GetWorldCollisionBox (skeletal bounds)
- [ ] Transient channel cleanup

## 2. Playable tutorial (Lev_Tut1)
- [x] TraceTexture at HP1's native index 285 (footsteps); decals not traced yet
- [ ] Play through Lev_Tut1 + Lev_Tut1b, fix what breaks (`tools/run_hp1.sh 60 --url=Lev_Tut1`)
- [ ] Missing Actor natives: ModifySound(567), StopSound(568), SaveGameExists(3972), Wind.GetWind,
      PlayerPawn.ScreenToWorld, Pawn.FindPath, Console.CreateNativeFont
- [ ] Unknown console command `Snap`

## 3. Spells
- [ ] `ParticleFX` native class (AddParticle, NumParticles, Get/SetParticleParams, RecomputeDeltas) + rendering
- [ ] `Gesture` spell-drawing recognition (CompareGesture, CompareGesturePoint)
- [ ] Spell targeting / eVulnerableToSpell paths

## 4. Save games and front end
- [ ] Save/LoadGameSaveInfo, Save/LoadObjectAsFile, SaveGameExists (`GameSaveInfo` class)
- [ ] CreateTextureFromScreenShot / CreateTextureFromBMP (save thumbnails)
- [ ] FEBook pages that depend on the above

## 5. Remaining native classes and polish
- [ ] Wind, ImpactSoundSet, SoundContainer, InterpolationManager, ClipMarker, LocationID
- [ ] Quidditch / broom levels
- [ ] Full game playthrough; per-level bug list
- [ ] Editor-only natives (BrushBuilders) — low priority

## Engine hooks (upstream files we touch)

Kept minimal so `tools/update_engine.sh` merges stay easy. Grep `hp1_re:` for the full list.

| File | Hook |
|---|---|
| `engine/CMakeLists.txt` | includes `hp1/hp1.cmake` |
| `Package/PackageManager.cpp` | `HP1::RegisterNatives()` after upstream natives |
| `Packages/Engine/Resources/Mesh/UAnimation.cpp` | `HP1::LoadAnimation` |
| `Packages/Engine/Actors/UActor_Animation.cpp` | `HP1::TickAnimation` |
| `UE1GameDatabase.h`, `GameApp.cpp` | exe hashes, `--autolaunch` / `--logfile` |
