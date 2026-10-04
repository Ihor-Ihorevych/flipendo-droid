# Small native classes and leftover natives

Native classes and natives that are not big enough for a topic note of their own: what Engine.dll has for them,
whether HP1's scripts reach them, and what Flipendo does.

## Native classes without native code

`ImpactSoundSet`, `SoundContainer`, `ClipMarker` and `LocationID` are declared `native` in Engine.u, but Engine.dll
only has the generated boilerplate for them: StaticClass, the two `operator new`, constructor, copy constructor,
assignment, destructor, InternalConstructor and the class registration (`UClass::UClass(...)` in a static
initializer, e.g. sub_1036D370 for ClipMarker). No other Engine.dll or Render.dll function refers to them. The
registered sizes are exactly the script fields, so there is no native-only state either:

| Class | Parent | Size | Script fields |
|---|---|---|---|
| SoundContainer | Object | 0x28 | none (UObject is 0x28); only the structs SoundSet and CreatureSoundGroup |
| ImpactSoundSet | SoundContainer | 0x528 | 20 `ImpactSoundEntry` (44-byte ImpactSoundProperties + 20-byte SoundSet) = 0x500 |
| ClipMarker | Keypoint | 0x25C | none (Keypoint is 0x25C); editor clip-plane marker |
| LocationID | Keypoint | 0x270 | LocationName (string, 12) + Radius (4) + NextLocation (4) = 0x14 |

SurrealEngine already registers `LocationID` as a native class; the other three are created from their script
class like any non-native class, which is all they need. LocationID's PostBeginPlay (script) chains it into its
zone's `ZoneInfo.LocationID` list; only Lev_Tut3 places one. Nothing in the shipped scripts uses ImpactSoundSet or
SoundContainer objects.

## Natives

| Native | Index | Script callers | Flipendo |
|---|---|---|---|
| `Pawn.FindStairRotation` | 524 | PlayerPawn.PlayerWalking's PlayerMove, only with `bLookUpStairs` (the `StairLook` exec, off by default). Harry has his own PlayerWalking that never calls it | ported, `kw/KWPlayerNatives.cpp` |
| `PlayerPawn.ScreenToWorld` | 542 | none | ported, `kw/KWPlayerNatives.cpp` |
| `Console.CreateNativeFont` | name | HPConsole, for the Asian languages (SIM, CHI, TRA, KOR, THA, JAP; fonts named in SAPFont.int) | returns None (see below) |
| `PlayerPawn.ResetKeyboard` | 544 | none | SurrealEngine's stub |
| `Pawn.actorReachable` | 520 | none | SurrealEngine's version |

**FindStairRotation** is stock UE1 behaviour: probes ahead along the view yaw from eye height (a box of the collision
radius, 1 unit tall, TRACE_Movers|TRACE_Level) for up to 2 × (CollisionHeight + BaseEyeHeight), then sweeps down
3 × that from the middle and from the end of the clear stretch. If the middle floor is more than 6 units above the
70% mark, the stairs go up: the target is 5400 when the far floor is another 10 units higher, else the current pitch
if it already looks up (0 if not). More than 6 units below means stairs going down, if a zero-extent trace at foot
level along 90% of the stretch is clear: -5000 when the far floor is 10 units lower still, else the current pitch if
it looks down. Anything else targets 0. The pitch eases towards the target at
rate 8 (or 8000/diff below 1000 units of difference, integer division) × DeltaTime. It normalizes the actor's own
ViewRotation.Pitch to -32768..32768 as a side effect. DeltaTime above 0.33 s leaves the pitch unchanged.

**ScreenToWorld** scales (2X - SizeX, 2Y - SizeY) by `S.Z * tan(FovAngle/2) / SizeX`, keeps S.Z as the third
component, calls the player's own PlayerCalcView (starting from its Location and Rotation) and returns
`CameraLocation + offset.TransformVectorBy(GMath.UnitCoords * CameraRotation)`. The screen axes go into the coords'
X/Y/Z slots as they are, which looks wrong for a forward-is-X frame; it is ported literally because nothing calls it.

**CreateNativeFont**: UConsole::execCreateNativeFont calls the viewport's virtual CreateNativeFont (vtable +156).
Engine.dll's base UViewport returns null; the real one is in WinDrv.dll's Windows viewport (GDI system font
rasterized into a UFont), which is not reversed. Until it is, Flipendo returns None like the base class, so the
Asian-language menus have no text.

**ResetKeyboard** resets the viewport's Input class config (`UObject::ResetConfig` on `Viewport->Input->Class`),
i.e. key bindings back to the defaults. SurrealEngine's ResetConfig is a stub too.
