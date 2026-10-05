# Procedural textures

KnowWonder-specific behaviour of the Fire.dll procedural textures. HP2's Fire.dll is 100% identical code to HP1's
([hp2_compare.md](../reports/hp2_compare.md)), so everything here holds for both games. Addresses are HP1's.

## IceTexture (`src/knowwonder/KWIceTexture.cpp`)

The source texture refracted through a "glass" texture. Each output pixel is a source pixel from the same row, shifted
sideways by the glass value under it; one of the two layers pans (U/VPosition) according to PanningStyle:

- MoveIce (the glass pans, `BlitTexIce` Fire 0x10505E90): `dest[y][x] = source[y][(glass[y+V][x+U] + x) & UMask]`
- otherwise (the source pans, `BlitIceTex` Fire 0x10506210): `dest[y][x] = source[y+V][(glass[y][x] + x + U) & UMask]`

The output takes the source's palette (`PostLoad`, Fire 0x10509F20; the sizes must match, a source of another size is
resampled once into a static LocalSource, not ported). SurrealEngine's UIceTexture only copied the source unchanged.

Timing: `UTexture::Tick` (Engine.dll 0x104217C0) calls `ConstantTimeTick` every tick when MaxFrameRate is 0, which steps
the ice by a fixed 1/120 s (TIME_FrameRateSync, the default); TIME_RealTimeScroll steps by the elapsed time instead.
Flipendo runs both once per texture frame (SurrealEngine's `UTexture::Update` schedules those).

HP1's spell lesson blackboard is an IceTexture ([spells.md](../hp1/spells.md#spell-lesson-rendering)).
