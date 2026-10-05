#include "Precomp.h"
#include "KW.h"
#include "KWMove.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include "Math/coords.h"
#include "Engine.h"
#include <cmath>

// InterpolationManager (Engine.InterpolationManager, native in HP1): spawned by Actor.StartInterpolation and by the
// broom/Quidditch scripts (BroomHarry, QuidPlayer, QuidditchPawn) to fly its Owner along a path of
// InterpolationPoints. The manager has PHYS_Interpolating; its native performPhysics moves the Owner (which sits in
// PHYS_None with bInterpolating) along cubic Bezier segments, and raises UpdateCamera, InterpolationPoint.InterpolateEnd
// and FinishedInterpolation. HP1's InterpolationPoint is not UT's: arrays per pause (Pause[8], ViewTargetTag[8], ...),
// Bezier control points, DesiredSpeed/PathDist. Upstream's TickInterpolating (UT keypoints) doesn't apply.
//
// FCoords here: "GMath.UnitCoords / Rotation" has XAxis = forward, which is Coords::Rotation(rot) in SurrealEngine.

namespace KW
{
	struct InterpProps
	{
		// InterpolationManager (PhysAlpha/PhysRate shadow Actor's)
		PropertyDataOffset Dest, PhysAlpha, PhysRate, RemainingPause, StartPause, PauseNum;
		PropertyDataOffset TurnRateX, TurnRateZ, OldDesiredX, OldDesiredZ, bInstantMove;
		// InterpolationPoint
		PropertyDataOffset bInstantNextPath, bFaceMoveDirection, bConstantSpeed, bTurnChange, bNewRotationSmoothing;
		PropertyDataOffset Pause, ViewTargetTag, ViewTarget, Prev, Next;
		PropertyDataOffset StartControlPoint, EndControlPoint, Smoothing, DesiredSpeed, PathDist;
		// Actor
		PropertyDataOffset IPSpeed;
		UClass* ManagerClass = nullptr;
	};

	// The class's own property of that name (the last one in the list), not a shadowed base class one.
	static PropertyDataOffset OwnProperty(UClass* cls, const NameString& name)
	{
		PropertyDataOffset result;
		for (UProperty* prop : cls->Properties)
			if (prop->Name == name)
				result = prop->DataOffset;
		if (result.DataOffset == ~(size_t)0)
			Exception::Throw("HP1: property " + name.ToString() + " not found in " + cls->Name.ToString());
		return result;
	}

	static const InterpProps& GetInterpProps()
	{
		static InterpProps p;
		static bool initialized = false;
		if (!initialized)
		{
			UClass* m = engine->packages->FindClass("Engine.InterpolationManager");
			UClass* ip = engine->packages->FindClass("Engine.InterpolationPoint");
			UClass* actor = engine->packages->FindClass("Engine.Actor");
			if (!m || !ip || !actor)
				Exception::Throw("HP1: InterpolationManager/InterpolationPoint class not found");
			p.ManagerClass = m;
			p.Dest = OwnProperty(m, "Dest");
			p.PhysAlpha = OwnProperty(m, "PhysAlpha");
			p.PhysRate = OwnProperty(m, "PhysRate");
			p.RemainingPause = OwnProperty(m, "RemainingPause");
			p.StartPause = OwnProperty(m, "StartPause");
			p.PauseNum = OwnProperty(m, "PauseNum");
			p.TurnRateX = OwnProperty(m, "TurnRateX");
			p.TurnRateZ = OwnProperty(m, "TurnRateZ");
			p.OldDesiredX = OwnProperty(m, "OldDesiredX");
			p.OldDesiredZ = OwnProperty(m, "OldDesiredZ");
			p.bInstantMove = OwnProperty(m, "bInstantMove");
			p.bInstantNextPath = OwnProperty(ip, "bInstantNextPath");
			p.bFaceMoveDirection = OwnProperty(ip, "bFaceMoveDirection");
			p.bConstantSpeed = OwnProperty(ip, "bConstantSpeed");
			p.bTurnChange = OwnProperty(ip, "bTurnChange");
			p.bNewRotationSmoothing = OwnProperty(ip, "bNewRotationSmoothing");
			p.Pause = OwnProperty(ip, "Pause");
			p.ViewTargetTag = OwnProperty(ip, "ViewTargetTag");
			p.ViewTarget = OwnProperty(ip, "ViewTarget");
			p.Prev = OwnProperty(ip, "Prev");
			p.Next = OwnProperty(ip, "Next");
			p.StartControlPoint = OwnProperty(ip, "StartControlPoint");
			p.EndControlPoint = OwnProperty(ip, "EndControlPoint");
			p.Smoothing = OwnProperty(ip, "Smoothing");
			p.DesiredSpeed = OwnProperty(ip, "DesiredSpeed");
			p.PathDist = OwnProperty(ip, "PathDist");
			p.IPSpeed = actor->GetPropertyDataOffset("IPSpeed");
			initialized = true;
		}
		return p;
	}

	// Typed views of the two script classes (both are plain UActor objects in SurrealEngine for HP1).
	struct IPoint
	{
		UActor* a;
		static const InterpProps& P() { return GetInterpProps(); }
		IPoint(UActor* actor) : a(actor) {}

		bool bInstantNextPath() const { return a->BoolValue(P().bInstantNextPath); }
		bool bFaceMoveDirection() const { return a->BoolValue(P().bFaceMoveDirection); }
		bool bConstantSpeed() const { return a->BoolValue(P().bConstantSpeed); }
		bool bTurnChange() const { return a->BoolValue(P().bTurnChange); }
		bool bNewRotationSmoothing() const { return a->BoolValue(P().bNewRotationSmoothing); }
		float Pause(int i) const { return (&a->Value<float>(P().Pause))[i]; }
		NameString& ViewTargetTag(int i) const { return (&a->Value<NameString>(P().ViewTargetTag))[i]; }
		UActor*& ViewTarget(int i) const { return (&a->Value<UActor*>(P().ViewTarget))[i]; }
		UActor* Prev() const { return a->Value<UActor*>(P().Prev); }
		UActor* Next() const { return a->Value<UActor*>(P().Next); }
		vec3 StartControlPoint() const { return a->Value<vec3>(P().StartControlPoint); }
		vec3 EndControlPoint() const { return a->Value<vec3>(P().EndControlPoint); }
		float Smoothing() const { return a->Value<float>(P().Smoothing); }
		float DesiredSpeed() const { return a->Value<float>(P().DesiredSpeed); }
		float PathDist() const { return a->Value<float>(P().PathDist); }
	};

	struct IManager
	{
		UActor* a;
		static const InterpProps& P() { return GetInterpProps(); }
		IManager(UActor* actor) : a(actor) {}

		UActor*& Dest() const { return a->Value<UActor*>(P().Dest); }
		float& PhysAlpha() const { return a->Value<float>(P().PhysAlpha); }
		float& PhysRate() const { return a->Value<float>(P().PhysRate); }
		float& RemainingPause() const { return a->Value<float>(P().RemainingPause); }
		float& StartPause() const { return a->Value<float>(P().StartPause); }
		int& PauseNum() const { return a->Value<int>(P().PauseNum); }
		vec3& TurnRateX() const { return a->Value<vec3>(P().TurnRateX); }
		vec3& TurnRateZ() const { return a->Value<vec3>(P().TurnRateZ); }
		vec3& OldDesiredX() const { return a->Value<vec3>(P().OldDesiredX); }
		vec3& OldDesiredZ() const { return a->Value<vec3>(P().OldDesiredZ); }
		BitfieldBool bInstantMove() const { return a->BoolValue(P().bInstantMove); }
	};

	static Coords RotationCoords(const Rotator& rot, const vec3& origin = vec3(0.0f))
	{
		Coords c = Coords::Rotation(rot);
		c.Origin = origin;
		return c;
	}

	// UE1 normalizes in place only when the squared size is >= 1e-8 (FVector::Normalize / the inlined copies).
	static vec3 SafeNormal(const vec3& v)
	{
		float sq = dot(v, v);
		return sq >= 1e-8f ? v * (1.0f / std::sqrt(sq)) : v;
	}

	// IDA Core.dll: ?OrthoRotation@FCoords@@QBE?AVFRotator@@XZ [HP1 Core 0x1014ED10]
	Rotator OrthoRotation(const vec3& x, const vec3& y, const vec3& z)
	{
		const float scale = 32768.0f / 3.14159265358979f;
		Rotator r;
		r.Yaw = (int)(std::atan2(x.y, x.x) * scale);
		r.Pitch = (int)(std::atan2(x.z, std::sqrt(x.x * x.x + x.y * x.y)) * scale);
		r.Roll = 0;
		vec3 py = Coords::Rotation(r).YAxis;
		r.Roll = (int)(std::atan2(dot(py, z), dot(py, y)) * scale);
		return r;
	}

	static Rotator OrthoRotation(const Coords& c) { return OrthoRotation(c.XAxis, c.YAxis, c.ZAxis); }

	// The first actor in the level with that Tag (the original walks XLevel->Actors the same way).
	static UActor* FindByTag(const NameString& tag)
	{
		for (UActor* a : engine->Level->Actors)
			if (a && a->Tag() == tag)
				return a;
		return nullptr;
	}

	static UActor* ResolveViewTarget(IPoint pt, int pauseNum)
	{
		const NameString& tag = pt.ViewTargetTag(pauseNum);
		if (tag.IsNone())
			return nullptr;
		UActor*& cached = pt.ViewTarget(pauseNum);
		if (!cached || cached->Tag() != tag)
		{
			UActor* found = FindByTag(tag);
			if (found)
				cached = found;
		}
		return cached;
	}

	// x87 fistp with the default round-to-nearest mode.
	static int RoundInt(float f) { return (int)std::nearbyint(f); }
	static Rotator ScaleRot(const Rotator& r, float s) { return Rotator(RoundInt(r.Pitch * s), RoundInt(r.Yaw * s), RoundInt(r.Roll * s)); }

	// IDA Engine.dll: ?GetDesiredRotationAtPause@AInterpolationPoint@@QAE?AVFRotator@@HVFVector@@@Z [HP1 0x103F6820]
	static Rotator GetDesiredRotationAtPause(IPoint pt, int pauseNum, const vec3& location)
	{
		UActor* target = ResolveViewTarget(pt, pauseNum);
		if (target)
			return Rotator::FromVector(target->Location() - location);
		return pt.a->Rotation();
	}

	// IDA Engine.dll: ?GetDesiredRotationAtPosition@AInterpolationPoint@@QAE?AVFRotator@@HHVFVector@@@Z [HP1 0x103F65A0]
	// offset < 0 walks back through the pauses of this point and then into Prev, offset > 0 forward into Next.
	// depth is ours: the original recurses without a limit, and on a closed path with no bInstantNextPath point
	// the forward walk never ends (only reachable with bFaceMoveDirection=False and bNewRotationSmoothing=True).
	static Rotator GetDesiredRotationAtPosition(IPoint pt, int offset, int pauseNum, const vec3& location, int depth = 0)
	{
		if (depth > 64)
			return GetDesiredRotationAtPause(pt, pauseNum, location);
		if (offset < 0)
		{
			if (pauseNum > 0)
				return GetDesiredRotationAtPosition(pt, offset + 1, pauseNum - 1, location, depth + 1);
			UActor* prev = pt.Prev();
			if (pt.bInstantNextPath() || !prev)
				return GetDesiredRotationAtPause(pt, pauseNum, location);
			IPoint p(prev);
			int i = 0;
			while (i < 7 && p.Pause(i) > 0.0f)
				i++;
			return GetDesiredRotationAtPosition(p, offset + 1, i, location, depth + 1);
		}
		else if (offset == 0)
		{
			return GetDesiredRotationAtPause(pt, pauseNum, location);
		}
		else
		{
			int p = pauseNum;
			while (p + 1 < 8 && pt.Pause(p + 1) > 0.0f && offset > 0)
			{
				p++;
				offset--;
			}
			UActor* next = pt.Next();
			if (next && offset != 0 && !IPoint(next).bInstantNextPath())
				return GetDesiredRotationAtPosition(IPoint(next), offset, 0, location, depth + 1);
			return GetDesiredRotationAtPause(pt, p, location);
		}
	}

	static vec3 Bezier(const vec3& p0, const vec3& c0, const vec3& c1, const vec3& p1, float t)
	{
		float u = 1.0f - t;
		return p0 * (u * u * u) + c0 * (u * u * t * 3.0f) + c1 * (u * t * t * 3.0f) + p1 * (t * t * t);
	}

	// IDA Engine.dll: ?GetInterpolatedPosition@AInterpolationPoint@@QAE?AVFCoords@@V2@VFVector@@MH@Z [HP1 0x103F69F0]
	// The segment from start (Prev's location and rotation) to this point: Bezier through start.Origin + startControl
	// and Location + EndControlPoint. Returns the location as Origin and the desired orientation as axes.
	static Coords GetInterpolatedPosition(IPoint pt, const Coords& start, const vec3& startControl, float t, int pauseNum)
	{
		UActor* self = pt.a;
		if (pt.bInstantNextPath())
			return RotationCoords(self->Rotation(), self->Location());

		vec3 p0 = start.Origin;
		vec3 c0 = start.Origin + startControl;
		vec3 c1 = self->Location() + pt.EndControlPoint();
		vec3 p1 = self->Location();
		vec3 pos = Bezier(p0, c0, c1, p1, t);

		Coords result;
		if (pt.bFaceMoveDirection())
		{
			float t0 = t - 0.001f;
			if (t0 > 0.0f)
				result = Coords::Rotation(Rotator::FromVector(pos - Bezier(p0, c0, c1, p1, t0)));
			else
				result = Coords::Rotation(pt.Prev() ? pt.Prev()->Rotation() : self->Rotation());
		}
		else if (pt.bNewRotationSmoothing())
		{
			// Catmull-Rom over the desired rotations around this segment: rm1 at the start, r0 at this point.
			Rotator rm2 = GetDesiredRotationAtPosition(pt, -2, pauseNum, pos);
			Rotator rm1 = GetDesiredRotationAtPosition(pt, -1, pauseNum, pos);
			Rotator r0 = GetDesiredRotationAtPosition(pt, 0, pauseNum, pos);
			Rotator r1 = GetDesiredRotationAtPosition(pt, 1, pauseNum, pos);
			float t2 = t * t, t3 = t2 * t;
			Rotator m1 = ScaleRot(r1 - rm1, (t3 - t2) * 0.5f);
			Rotator m0 = ScaleRot(r0 - rm2, (t3 - 2.0f * t2 + t) * 0.5f);
			Rotator h1 = ScaleRot(r0, 3.0f * t2 - 2.0f * t3);
			Rotator h0 = ScaleRot(rm1, 2.0f * t3 - 3.0f * t2 + 1.0f);
			result = Coords::Rotation(h0 + h1 + m0 + m1);
		}
		else if (pt.ViewTargetTag(pauseNum).IsNone())
		{
			// Blend the start axes into this point's; bTurnChange eases in faster.
			float w = pt.bTurnChange() ? std::sqrt(t) : t;
			Coords d = Coords::Rotation(self->Rotation());
			result.XAxis = SafeNormal(d.XAxis * w + start.XAxis * (1.0f - t));
			vec3 z = d.ZAxis * w + start.ZAxis * (1.0f - t);
			result.YAxis = SafeNormal(cross(z, result.XAxis));
			result.ZAxis = SafeNormal(cross(result.XAxis, result.YAxis));
		}
		else
		{
			UActor* target = ResolveViewTarget(pt, pauseNum);
			result = target ? Coords::Rotation(Rotator::FromVector(target->Location() - pos)) : start;
		}
		result.Origin = pos;
		return result;
	}

	bool IsInterpolationManager(UActor* actor)
	{
		return actor->IsA("InterpolationManager");
	}

	// IDA Engine.dll: ?performPhysics@AInterpolationManager@@UAEXM@Z [HP1 0x103F7BA0]
	void InterpolationManagerPhysics(UActor* self, float deltaTime)
	{
		IManager m(self);
		if (deltaTime <= 0.0f)
			return;
		UActor* owner = self->Owner();
		if (!owner || owner->bDeleteMe())
		{
			self->Destroy();
			return;
		}
		const InterpProps& props = GetInterpProps();
		float& ownerIPSpeed = owner->Value<float>(props.IPSpeed);

		vec3 startLocation = owner->Location();
		vec3 startVelocity = owner->Velocity();
		float ipSpeed = 0.0f;
		float timeLeft = deltaTime;
		// The original keeps the FCoords in one function-wide local; a pause without a view target reuses
		// whatever it held. We start from the owner's orientation.
		Coords desired = RotationCoords(owner->Rotation(), owner->Location());

		while (m.PhysRate() != 0.0f && owner->bInterpolating() && timeLeft > 0.0f && !self->bDeleteMe())
		{
			float speedScale = 1.0f;
			vec3 oldLocation = owner->Location();

			UActor* dest = m.Dest();
			if (dest)
			{
				IPoint d(dest);
				if (ownerIPSpeed == 0.0f)
				{
					if (d.Next())
					{
						float speed = d.DesiredSpeed() > 0.0f ? d.DesiredSpeed() : length(owner->Velocity());
						IPoint n(d.Next());
						if (n.DesiredSpeed() > 0.0f)
							ipSpeed = (1.0f - m.PhysAlpha()) * speed + n.DesiredSpeed() * m.PhysAlpha();
						else if (d.DesiredSpeed() > 0.0f)
							ipSpeed = d.DesiredSpeed();
						else
							ipSpeed = 0.0f;
					}
				}
				else
				{
					ipSpeed = ownerIPSpeed;
				}

				if (ipSpeed <= 0.0f)
				{
					// No speed: scale by how the control point lengths change over the segment.
					// (A zero StartControlPoint divides by zero in the original; we keep the scale at 1.)
					float startLen = length(d.StartControlPoint());
					if (startLen > 0.0f)
						speedScale = (length(d.EndControlPoint()) / startLen - 1.0f) * m.PhysAlpha() + 1.0f;
				}
				else if (d.PathDist() > 0.0f)
				{
					speedScale = ipSpeed / d.PathDist();
				}
			}

			float step = speedScale * m.PhysRate() * timeLeft;
			float oldAlpha = m.PhysAlpha();
			float newAlpha = step + oldAlpha;
			m.PhysAlpha() = std::clamp(newAlpha, 0.0f, 1.0f);
			bool reachedEnd = false;
			// "Move time" for the speed and turn rate maths. The original uses 1 - oldAlpha (an alpha, not a time)
			// when the step passes the end of the segment.
			float moveTime = timeLeft;
			if (newAlpha > 1.0f && m.RemainingPause() <= 0.0f)
				moveTime = step - newAlpha + 1.0f;

			vec3 location = owner->Location();
			if (dest)
			{
				IPoint d(dest);
				if (m.RemainingPause() <= 0.0f)
				{
					CallEvent(self, NameString("UpdateCamera"), { ExpressionValue::FloatValue(oldAlpha) });
					dest = m.Dest();
					if (!dest || self->bDeleteMe())
						break;
					d = IPoint(dest);

					Coords start;
					vec3 startControl(0.0f);
					if (UActor* prev = d.Prev())
					{
						start = RotationCoords(prev->Rotation(), prev->Location());
						startControl = IPoint(prev).StartControlPoint();
					}
					else
					{
						start = RotationCoords(dest->Rotation(), owner->Location());
					}

					desired = GetInterpolatedPosition(d, start, startControl, m.PhysAlpha(), m.PauseNum());
					location = desired.Origin;

					if (d.bConstantSpeed() && ipSpeed > 0.0f && m.PhysAlpha() < 1.0f && !d.bInstantNextPath())
					{
						// Correct alpha once so the actual speed along the curve matches IPSpeed (within 5%).
						float actualSpeed = length(desired.Origin - oldLocation) / moveTime;
						if (std::abs(actualSpeed - ipSpeed) > ipSpeed * 0.05f)
						{
							newAlpha = moveTime * m.PhysRate() * speedScale * ipSpeed / actualSpeed + oldAlpha;
							m.PhysAlpha() = std::clamp(newAlpha, 0.0f, 1.0f);
							desired = GetInterpolatedPosition(d, start, startControl, m.PhysAlpha(), m.PauseNum());
							location = desired.Origin;
							if (newAlpha > 1.0f && m.RemainingPause() <= 0.0f)
								moveTime = speedScale * m.PhysRate() * timeLeft - newAlpha + 1.0f;
						}
					}

					if (d.bFaceMoveDirection())
					{
						desired = Coords::Rotation(Rotator::FromVector(desired.Origin - owner->Location()));
						desired.Origin = location;
					}
				}
				else
				{
					ipSpeed = 0.0f;
					m.PhysAlpha() = 0.0f;
					newAlpha = 0.0f;
					m.RemainingPause() -= timeLeft;
					if (m.RemainingPause() < 0.0f)
					{
						m.RemainingPause() = 0.0f;
						timeLeft = 0.0f;
						reachedEnd = true;
					}
					CallEvent(self, NameString("UpdateCamera"), { ExpressionValue::FloatValue((m.StartPause() - m.RemainingPause()) / m.StartPause()) });
					dest = m.Dest();
					if (!dest || self->bDeleteMe())
						break;
					d = IPoint(dest);
					if (UActor* target = ResolveViewTarget(d, m.PauseNum()))
						desired = Coords::Rotation(Rotator::FromVector(target->Location() - owner->Location()));
					desired.Origin = owner->Location();
				}

				if (!d.bNewRotationSmoothing())
				{
					// Smooth the turn: the axes follow a turn rate that eases towards the desired one
					// (Prev.Smoothing: 0 = no smoothing, larger = slower).
					Coords cur = Coords::Rotation(owner->Rotation());
					float inv = 1.0f / moveTime;
					vec3 desRateX = (desired.XAxis - m.OldDesiredX()) * inv;
					vec3 desRateZ = (desired.ZAxis - m.OldDesiredZ()) * inv;
					m.OldDesiredX() = desired.XAxis;
					m.OldDesiredZ() = desired.ZAxis;

					float k = 1.0f;
					if (UActor* prev = d.Prev())
					{
						float s = IPoint(prev).Smoothing();
						k = s == 0.0f ? 0.0f : 1.0f / s;
					}
					if (k != 0.0f)
					{
						vec3 needX = (desired.XAxis - cur.XAxis) * (1.0f / k);
						vec3 needZ = (desired.ZAxis - cur.ZAxis) * (1.0f / k);
						if (dot(needX, desRateX) <= 0.0f)
							desRateX = needX;
						if (dot(needZ, desRateZ) <= 0.0f)
							desRateZ = needZ;

						float a = k * moveTime;
						m.TurnRateX() = m.TurnRateX() * (1.0f - a) + desRateX * a;
						m.TurnRateZ() = m.TurnRateZ() * (1.0f - a) + desRateZ * a;
						desired.XAxis = SafeNormal(cur.XAxis + m.TurnRateX() * moveTime);
						desired.ZAxis = SafeNormal(cur.ZAxis + m.TurnRateZ() * moveTime);
						desired.YAxis = SafeNormal(cross(desired.ZAxis, desired.XAxis));
						desired.ZAxis = SafeNormal(cross(desired.XAxis, desired.YAxis));
					}
					m.TurnRateX() = (desired.XAxis - cur.XAxis) * inv;
					m.TurnRateZ() = (desired.ZAxis - cur.ZAxis) * inv;
				}

				// XLevel->MoveActor(Owner, Delta, NewRotation, Hit)
				if (UseKWPhysics())
				{
					CheckResult hit;
					MoveActor(owner, location - owner->Location(), OrthoRotation(desired), hit);
				}
				else
				{
					owner->SetRotation(OrthoRotation(desired));
					owner->TryMove(location - owner->Location());
				}
			}
			else
			{
				reachedEnd = true;
			}

			bool forward;
			if (m.PhysRate() <= 0.0f)
			{
				forward = false;
				if (newAlpha < 0.0f)
				{
					timeLeft = -newAlpha / (oldAlpha - newAlpha) * timeLeft;
					m.PhysAlpha() = 1.0f;
					reachedEnd = true;
				}
			}
			else
			{
				forward = true;
				if (newAlpha > 1.0f)
				{
					timeLeft = (newAlpha - 1.0f) / (newAlpha - oldAlpha) * timeLeft;
					m.PhysAlpha() = 0.0f;
					reachedEnd = true;
				}
			}

			if (!reachedEnd)
			{
				timeLeft = 0.0f;
				continue;
			}

			if (UActor* end = m.Dest())
				CallEvent(end, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(self), ExpressionValue::BoolValue(forward) });
			else
				CallEvent(self, NameString("FinishedInterpolation"), { ExpressionValue::ObjectValue(nullptr) });

			if (m.bInstantMove())
			{
				// InstantMove teleported the owner: measure speed from there.
				m.bInstantMove() = false;
				startLocation = owner->Location();
				oldLocation = owner->Location();
			}
			owner->Velocity() = (owner->Location() - oldLocation) / deltaTime;
			if (length(owner->Velocity()) >= ipSpeed)
				timeLeft = 0.0f;
		}

		// Cap the frame's speed at 105% of IPSpeed (the alpha correction can overshoot on tight curves).
		owner->Velocity() = (owner->Location() - startLocation) / deltaTime;
		float speed = length(owner->Velocity());
		if (ipSpeed > 0.0f && ipSpeed * 1.05f < speed)
		{
			vec3 target = startLocation + (owner->Location() - startLocation) * (ipSpeed / speed);
			if (UseKWPhysics())
			{
				CheckResult hit;
				MoveActor(owner, target - owner->Location(), owner->Rotation(), hit);
			}
			else
			{
				owner->TryMove(target - owner->Location());
			}
			owner->Velocity() = (owner->Location() - startLocation) / deltaTime;
		}
		owner->Acceleration() = (owner->Velocity() - startVelocity) / deltaTime;
	}
}
