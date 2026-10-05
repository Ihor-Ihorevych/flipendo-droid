# Skeletal animation

KnowWonder's skeletal meshes and animation system, which both games use for every character: `UAnimation`,
`USkeletalMesh`, animation channels, tweening as a blend weight, and root motion. Implementation: `src/knowwonder/Anim/`.

Reversed from HP1's `Engine.dll` (`../ida/hp1/Engine.dll.i64`; layouts measured from `Serialize` and field accesses).
Addresses below are HP1's.

## HP1 and HP2

Both games run the same system ([hp2_compare.md](../reports/hp2_compare.md)):

- **Identical in HP2:** the data and its loading (`UAnimation::Serialize`, `USkeletalMesh::Serialize`, `GetAnimSeq`,
  `GetMovement`, `BoneName`), skinning (`GetFrame`), `SlerpQuat` and `FCoords(FPlace)` in Core.dll, and the latent
  `execPollFinishAnim`.
- **Offsets only:** `IsAnimating`, `AdjustRootMovement`.
- **Changed, HP2 code not read yet:** `PlayAnim` (+205 bytes), `AActor::Tick` (+220), `ApplyAnim`, `GetMeshCoords`,
  `GetBoneCoords`, `GetRootMovement`. The `exec*` wrappers grew by the ~29 bytes of HP2's DebugInfo check
  ([scripting.md](scripting.md)), which isn't a behaviour change.
- **Signature change:** HP2's `CreateAnimChannel` takes one more bool (`bNotReplaceable` in HP2's `Actor.uc`; export
  `...VFName@@_N3@Z` instead of HP1's `...VFName@@_N@Z`). Not reversed yet.
- **HP2 only:** `Actor.BoneRot` (native 328, `src/hp2/HP2Natives.cpp`).

## Data: `UAnimation` (sizeof 156)

`UAnimation::Serialize` order (all counts are `FCompactIndex`):

| Field | Element | Notes |
|---|---|---|
| `RefBones` | bone {FName Name; DWORD Flags; INT ParentIndex} | |
| `Moves` | `MotionChunk` (48 B) {FVector RootSpeed3D; FLOAT TrackTime; INT StartBone; DWORD Flags; TArray<INT> BoneIndices; TArray<track> AnimTracks} | one per AnimSeq, same order. No separate root track |
| `AnimSeqs` | `FMeshAnimSeq` (32 B) {Name, Group, StartFrame, NumFrames, Notifys, Rate} | stock serialize order; in-memory Rate is at +16, Notifys at +20 |
| `KeyQuats` | `FAnimVec` (3x SWORD) | bulk |
| `KeyPoses` | `FAnimVec` | bulk |
| `KeyTimes` | BYTE | bulk |

Track (36 B): {DWORD Flags; KeyQuat (FAnimVec); KeyPos (FAnimVec); KeyTime (BYTE); FLOAT KeyPosScale; FLOAT KeyTimeScale}.
The three key arrays serialize only their counts; after load, `Serialize` walks all
tracks in order and points each one at the next slice of the shared pools.

`FAnimVec` decoding (`FAnimVec::Quat`, `FAnimVec::Vector`):
- quaternion: xyz = sin(v * (pi/2)/32767) (constant 0.0000479383634), w = sqrt(max(0, 1 - x²-y²-z²)); encoder flips sign so w >= 0
- position: v * Scale / 32767, Scale = track.KeyPosScale
- key time: raw byte, scaled by KeyTimeScale (exact use still to confirm in `ApplyAnim`)

## Data: `USkeletalMesh` (sizeof 684)

Serialize order matches SurrealEngine's loader: ULodMesh, ExtWedges, Points, RefSkeleton (bone, 64 B
in memory: Name, Flags, BonePos{Orientation, Position, Length, XSize, YSize, ZSize}, ParentIndex @52, NumChildren @56,
Depth @60; on disk NumChildren comes before ParentIndex), BoneWeightIdx, BoneWeights, LocalPoints, SkeletalDepth,
DefaultAnimation, WeaponBoneIndex, WeaponAdjust. `MeanBoundingBox` is computed on load (not serialized).

## Sequence lookup: `AActor::GetAnim`

Skeletal mesh: `(SkelAnim ? SkelAnim : Mesh.DefaultAnimation)->GetAnimSeq(name)`. Otherwise the classic
`UMesh::GetAnimSeq`. No fallback to the first sequence (SurrealEngine's `UMesh::GetSequence` has one).
`LinkSkelAnim(Anim)` just sets `SkelAnim`.

## Actor animation state

`execPlayAnim` → `AActor::PlayAnim(Seq, bLoop=false, Rate=1, TweenTime=-1, MinRate=0, Type, RootBone)`;
`execLoopAnim` → same with bLoop=true; `execTweenAnim(Seq, Time)` → `PlayAnim(Seq, false, 0, Time, 0, AT_Replace, None)`.

`PlayAnim`:
- no Mesh → log `PlayAnim: No mesh`, return.
- `RootBone=='Move'` → RootBone=None, bAnimMove=true; else bAnimMove=false.
- skeletal + RootBone: Seq None → stop channels in the bone's subtree (transient: destroy + remove from
  AuxAnims; persistent: AnimSequence=None). Else `CreateAnimChannel(AnimChannel, Type, RootBone, true)` and
  PlayAnim on the channel with AT_Replace / no RootBone.
- `Type==AT_Replace` → clear all aux channels the same way.
- sequence not found (and Seq != None) → log `PlayAnim: Sequence '%s' not found in Mesh '%s'`, return.
- LoopAnim on the already looping sequence: only update AnimRate/AnimMinRate, bAnimFinished=false.
- AnimRate = Rate*seq.Rate/NumFrames; AnimLast = 1-1/NumFrames; AnimMinRate = MinRate ? seq.Rate/NumFrames*MinRate : 0;
  bAnimNotify = Notifys.Num>0; bAnimLoop = bLoop; AnimFrame = 0; TweenAlpha = 0; AnimSequence = Seq.
- TweenTime > 0: TweenRate = 1/TweenTime; == 0: TweenRate 0, TweenAlpha 1; < 0: TweenRate = 2 (0.5 s).
- Then SimAnim packing for replication (not ported).

So HP tweening is a blend weight (`TweenAlpha`), not UE1's negative AnimFrame.

`CreateAnimChannel(Class, Type, RootBone, bTransient)`: needs skeletal mesh and a valid bone. Reuses an aux channel
with the same AnimBone and bAnimTransient. Else spawns `Class` (Owner=self, at self), copies Mesh and SkelAnim,
sets AnimBone=bone index, bAnimTransient. AT_Combine: insert at AuxAnims[0]; AT_Replace: destroy the channels in
[bone, bone+RefSkeleton[bone].NumChildren) and append.

`IsAnimating(RootBone)`: AnimSequence != None && (AnimRate != 0 || TweenRate != 0), on self or on the channel
whose AnimBone == BoneIndex(RootBone).

`FinishAnim(RootBone)`: target = self or that channel. If bAnimLoop: clear bAnimLoop and bAnimFinished. If animating
and AnimFrame < AnimLast: target.StateFrame.LatentAction = 385 (execPollFinishAnim, which ends when
bAnimFinished). With a RootBone the latent goes on the channel, so the caller does not wait.

## Tick (`AActor::Tick`, anim part)

Animator = Owner if (AnimBone != 0 && bAnimTransient) else self. Up to 4 iterations while animating and dt > 0:
1. TweenRate > 0: TweenAlpha += dt*TweenRate; at >= 1: clamp, TweenRate=0, and if AnimRate == 0 → bAnimFinished,
   animator.AnimEnd. dt isn't consumed here (a tween-only anim tweens up to 4x per tick).
2. AnimRate != 0: AnimFrame += rate*dt (negative AnimRate: max(AnimMinRate, -AnimRate*|animator.Velocity|)).
3. bAnimNotify: earliest notify with old < Time <= AnimFrame → AnimFrame = Time, dt = remaining, call it on the
   animator, next iteration.
4. AnimFrame < AnimLast → done. Looping: wrap to 0 once >= 1 (keeping the leftover dt); when crossing AnimLast:
   bAnimFinished if latent FinishAnim is pending, AnimEnd on self. Not looping: AnimFrame = AnimLast, AnimRate = 0,
   bAnimFinished, animator.AnimEnd.
AnimEnd is skipped for bSimulatedPawn (network only).

## Pose: `USkeletalMesh::ApplyAnim(Owner, Header, bRootOnly)`

Per-actor cache (`CFSkelHeader`, in GCache keyed by actor index): Mesh, linked UAnimation, last
AnimFrame/TweenAlpha/AnimSequence, a "has pose" flag, BoneMap[numBones] (mesh bone -> animation track,
matched by bone name; track index == animation RefBones index), Places[numBones] (FPlace: quat + pos),
MeshCoords (GetMeshCoords), root motion accumulators.

- SkelAnim null → set it to Mesh.DefaultAnimation (persistent side effect).
- time = min(AnimFrame, 1) * Move.TrackTime.
- Track sampling: KeyTime.Num == 1 → key 0. Else KeyTime[k] (k >= 1) * KeyTimeScale is the delta from key k-1
  (key 0 at time 0). Find the first key with time > t; prev = the one before. Exactly on prev → prev. Past the
  last key → interpolate from the last key to key 0 at TrackTime. Quat: SlerpQuat; pos: lerp (KeyPos.Num == 1
  → KeyPos[0] for every key).
- Bone 0: if the mesh root isn't the animation root, the ancestors' keys are folded in
  (quat = -(child * parent), pos = FCoords(parent) applied to the child pos).
- bAnimMove: root position replaced by the reference pose position; its movement is banked for GetRootMovement.
- Unmapped bones: main actor → reference pose; channels skip them.
- Tween: a = (has pose && TweenRate != 0) ? 1 - TweenAlpha : 0; place = Slerp(new, previous place, a),
  pos = new*(1-a) + prev*a. The "previous place" is the last evaluated pose (not a snapshot).
- Channels: after the main pose, each AuxAnims entry runs ApplyAnim(channel, mainHeader), mapping only bones
  in [AnimBone, AnimBone + NumChildren] (inclusive) and writing them into the main pose. A transient channel
  that is not looping and has AnimFrame >= AnimLast is destroyed and removed here.

## Frame: `USkeletalMesh::GetFrame(Verts, Size, Coords, Owner, LODRequest)`

Animator = Owner.Owner if bAnimByOwner. Bone coords: B[0] = FCoords(Place[0]), B[i] = B[parent] ∘ FCoords(Place[i]),
everything ∘ MeshCoords and the inverse of the camera Coords. Skinning: one BoneWeightIdx entry → all
LocalPoints by bone 0; else for each bone n, weights k in [WeightIndex, WeightIndex+Number):
Verts[BoneWeights[k].PointIndex] += B[n](LocalPoints[k]) * BoneWeight/65535.

Core.dll math (KnowWonder additions): `FCoords(FPlace)` standard quat→matrix (axes are the matrix rows);
`A /= B` (FCoords) = apply A then B; `SlerpQuat` shortest path + renormalize.

## Placement: `USkeletalMesh::GetMeshCoords`

World = Location + Rotation( PrePivot + MeshAdjust + RotOrigin( Scale' * (p - Mesh.Origin) ) ) with
Scale' = (Mesh.Scale.x*DrawScale*W, -Mesh.Scale.y*DrawScale*W, Mesh.Scale.z*DrawScale), W = Wideness/128.
Note the **negated Y** (skeletal meshes are mirrored) and that PrePivot is rotated with the actor.
MeshAdjust (bAlignBottom && bCollideWorld && Physics != 0 && CollideType != CT_Box?): z =
(Mesh.Origin.z - Mesh.BoundingBox.Min.z) * Mesh.Scale.z * DrawScale - (CollisionHeight + 2.5).

## Root motion (`bAnimMove`)

`PlayAnim(..., RootBone='Move')` sets bAnimMove. CFSkelHeader fields (float index): +27..29 LastRootPos, +30
RootLastFrame, +31..33 banked movement, +34..36 adjust bank, byte +148 "LastRootPos valid"; +8/+12/+16 are the last
evaluated AnimFrame/TweenAlpha/AnimSequence (the early-out: a full ApplyAnim with all three unchanged skips the pose,
so the tween blend can't run twice for one frame).

- `ApplyAnim(Owner, Header, bRootOnly)`: the bool is "root only" (bone 0, no aux channels, doesn't record the
  evaluation). A full evaluation of a new AnimSequence clears the valid flag and RootLastFrame.
- Bone 0 with bAnimMove: if not valid, LastRootPos = the root track's key 0 folded through its ancestors at time 0 and
  the adjust bank is cleared. Then bank += pose position - LastRootPos, LastRootPos = pose position, and the pose
  position is replaced by the reference position (the mesh stays put; the actor moves). If the sequence is the last
  evaluated one and AnimFrame changed: pay out the adjust bank, clamped per axis to
  |KeyPos[last] - KeyPos[0]| * (AnimFrame (1 if 0) - RootLastFrame); RootLastFrame = AnimFrame.
- `GetRootMovement` (0x1041EE20): ApplyAnim(root only), bank turned to world space with the mesh coords
  (rotation, scale, Y mirror), bank cleared.
- `AdjustRootMovement` (0x1041EFF0): world vector dotted with the mesh axes, added to the adjust bank.
- `AnimCycleMovement` (0x1041F170): KeyPos[last] - KeyPos[0] of the root track; no caller found (not ported).
- `AActor::Tick`, the end (bAnimMove && Role == ROLE_Authority): delta = GetRootMovement; MoveActor(delta); if it
  hit something, slide once along the hit normal; AdjustRootMovement(what wasn't moved).

Harry's climbs (`Mounting`/`MountFinish`) rely on it: the script moves only MountDelta minus the climb anims' own
movement (30 forward, 32/64/96 up).

## Bones and attachments

Field offsets (from `USkeletalMesh::Serialize`): WeaponBoneIndex @532 (INT, -1 = none), WeaponAdjust @536 (FCoords:
Origin, X, Y, Z), the weapon coords cache @636.

- `USkeletalMesh::GetBoneCoords(Actor, Bone)` (0x1041F3C0): ApplyAnim (root only for bone 0); invalid bone → GetMeshCoords;
  else C = FCoords(Place[bone]), then C = C /= FCoords(Place[parent]) up to the root, then C /= MeshCoords. Origin is the
  bone's world position, the axes carry the mesh scale and the Y mirror.
- `execBonePos(Bone)` (0x1040A5F0): skeletal mesh → GetBoneCoords(BoneIndex(Bone)).Origin; else Location.
- `physTrailer` (0x103F5F80): non-sprite with AnimBone → Owner.Mesh->GetBoneCoords(Owner, AnimBone-1): location = Origin,
  rotation = OrthoRotation. Otherwise location = Owner.Location (+ PrePivot rotated by Owner.Rotation if
  bTrailerSameRotation or bTrailerPrePivot), rotation = Owner.Rotation (bTrailerSameRotation), (-Velocity).Rotation()
  if the owner moves, else Pitch 0x4000. Sprites: + PrePivot (bTrailerPrePivot), or Location - forward*Mass
  (bTrailerSameRotation), or Location. Script `AttachToOwner(Bone)` = PHYS_Trailer + AnimBone = BoneNumber(Bone)+1.
- Weapon: end of `GetFrame`, if WeaponBoneIndex >= 0: W = WeaponAdjust /= B[WeaponBoneIndex] (camera space);
  X = SafeNormal(X), Y = SafeNormal(X ^ Z), Z = X ^ Y, Y = -Y; its inverse goes into the cache @636. Render.dll
  `DrawLodMesh` copies the cache into its weapon coords (instead of a weapon triangle) and `DrawActorSprite`, after a
  pawn, sets WeaponLoc/WeaponRot from it and draws Weapon.ThirdPersonMesh there (Mesh/DrawScale swapped with
  ThirdPersonMesh/ThirdPersonScale, Rotation zeroed, Location added to the coords origin) unless the weapon is bHidden.
  HP1 meshes have no weapon triangles (skharryMesh: WeaponBoneIndex 48, skronMesh 40).

## Still to reverse
- Who destroys finished transient channels.
- `Actor.Fatness` (default 128; not ported). In stock UE1 a vertex moves along its normal by `Fatness / 16 - 8`, off
  at 128. Whether HP1's skeletal path applies it, and whether any map or script changes it, is not checked.
