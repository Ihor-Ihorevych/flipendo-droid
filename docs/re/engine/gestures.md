# Gestures (spell shapes)

`Engine.Gesture` is KnowWonder's native class for a spell shape drawn with the mouse. `Points` is the template polyline
in 0..1 screen space (Z unused); `Segments` is unused by the natives. Ported in `src/knowwonder/KWGesture.cpp`. Addresses are HP1's.

**HP1 and HP2.** Both games' `Gesture.uc` declare `CompareGesture` (426) and `CompareGesturePoint` (427), and both
DLLs have them (changed by about the size of HP2's DebugInfo check, +52 and +29 bytes;
[hp2_compare.md](../reports/hp2_compare.md)). Only HP1 calls them, from `SpellLearnTrigger` and `Target`
([spells.md](../hp1/spells.md)). HP2's decompiled scripts never call them (no native 426/427 call anywhere);
HP2's `SpellCursor` shows a `GestureSprite` picture of the spell's shape instead. How HP2's lessons
(`SpellLessonTrigger`) judge the player isn't studied yet.

## Template sampling

Both natives resample the template polyline at 8 points per segment (the last point once): 8 * N - 7 samples. The
original caps N at 1024 (and overruns its 1024-sample stack buffer above 128 points).

## CompareGesture(InMousePoints, fAccuracy) (0x103A3F60)

Returns a match quality in 0..1 for the drawn points (screen 0..1 units; Z = -1 marks a point to skip). The original
intends to drop duplicate drawn points (within 0.001), but its inner loop compares each point with the next unwritten
slot of its stack buffer instead of the points kept so far, so in practice nothing is dropped.

The score (`sub_103A39C0`, not exported; only caller `execCompareGesture`, via thunk `sub_1030342C`), with d = distance to
the nearest point of the other set, capped at 1:

- every template sample: d <= accuracy adds 2 to the total; otherwise a penalty `min(int(d / (accuracy*5) + 1), 2)` is
  added to both bad and total;
- every drawn point with d > accuracy: penalty `min(int(d / accuracy), 3)` to both;
- fewer than 10 drawn points: 500 to both;
- result `clamp(1 - bad/total, 0, 1)` (total 0, which the original divides as 0/0, gives 0 here).

## CompareGesturePoint(InMousePoint, fAccuracy) (0x103A3CC0)

The distance (capped at 1) from the point to the nearest template sample. fAccuracy is read but unused.
