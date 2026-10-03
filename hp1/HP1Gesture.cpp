#include "Precomp.h"
#include "HP1.h"
#include "Packages/Core/UObject.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "VM/NativeFunc.h"
#include "Engine.h"
#include "HP1ParticleFX.h"
#include <cmath>

// Gesture (native class in HP1 Engine.dll): a spell shape drawn with the mouse. Points is the template
// polyline in 0..1 screen space (Z unused); SpellLearnTrigger and Target compare the player's drawn
// points against it. Segments is unused by the natives.

namespace HP1
{
	void OverrideNative(int index, void (*registerFunc)());

	TypedScriptArray<vec3> GesturePoints(UObject* gesture)
	{
		static PropertyDataOffset offset;
		static bool initialized = false;
		if (!initialized)
		{
			UClass* cls = engine->packages->FindClass("Engine.Gesture");
			if (!cls)
				Exception::Throw("HP1: Engine.Gesture class not found");
			offset = cls->GetPropertyDataOffset("Points");
			initialized = true;
		}
		return gesture->DynamicArray<vec3>(offset);
	}

	namespace
	{
		struct Point2 { float x, y; };

		// Both natives resample the template polyline at 8 points per segment (the last point once):
		// 8 * N - 7 samples. The original caps N at 1024 (and overruns its 1024 sample stack buffer above 128).
		std::vector<Point2> SampleTemplate(UObject* gesture)
		{
			TypedScriptArray<vec3> points = GesturePoints(gesture);
			int count = (int)std::min<size_t>(points.size(), 1024);
			std::vector<Point2> samples;
			if (count <= 0)
				return samples;
			samples.reserve(8 * count - 7);
			for (int i = 0; i < count; i++)
			{
				const vec3& p = points[i];
				samples.push_back({ p.x, p.y });
				if (i + 1 < count)
				{
					const vec3& next = points[i + 1];
					float dx = next.x - p.x;
					float dy = next.y - p.y;
					for (int k = 1; k < 8; k++)
						samples.push_back({ dx * (k * 0.125f) + p.x, dy * (k * 0.125f) + p.y });
				}
			}
			return samples;
		}

		float NearestDistance(const Point2& p, const std::vector<Point2>& list)
		{
			float best = 1.0f;
			for (const Point2& q : list)
			{
				float d = std::sqrt((q.y - p.y) * (q.y - p.y) + (q.x - p.x) * (q.x - p.x));
				if (d < best)
					best = d;
			}
			return best;
		}

		// IDA Engine.dll: not exported: sub_103A39C0 [HP1 0x103A39C0] (only caller: execCompareGesture, via thunk sub_1030342C)
		float ScoreGesture(const std::vector<Point2>& drawn, const std::vector<Point2>& samples, float accuracy)
		{
			int bad = 0;
			int total = 0;

			// Template coverage: every template sample should have a drawn point within accuracy.
			for (const Point2& s : samples)
			{
				float d = NearestDistance(s, drawn);
				if (d <= accuracy)
				{
					total += 2;
				}
				else
				{
					int penalty = std::min((int)(d / (accuracy * 5.0f) + 1.0f), 2);
					bad += penalty;
					total += penalty;
				}
			}

			// Stray strokes: every drawn point should be near the template.
			for (const Point2& p : drawn)
			{
				float d = NearestDistance(p, samples);
				if (d > accuracy)
				{
					int penalty = std::min((int)(d / accuracy), 3);
					bad += penalty;
					total += penalty;
				}
			}

			// Too short a stroke never matches well.
			if (drawn.size() < 10)
			{
				total += 500;
				bad += 500;
			}

			if (total == 0) // all template samples matched with no drawn points: original divides 0/0
				return 0.0f;
			float result = 1.0f - (float)((double)bad / (double)total);
			return std::clamp(result, 0.0f, 1.0f);
		}
	}

	// Returns a match quality in 0..1 for the drawn points (screen 0..1 units; Z = -1 marks a point to skip).
	// The original intends to drop duplicate drawn points (within 0.001), but its inner loop compares each
	// point with the next unwritten slot of its stack buffer instead of the points kept so far, so in
	// practice nothing is dropped. All non-skipped points are kept here.
	// IDA Engine.dll: ?execCompareGesture@UGesture@@QAEXAAUFFrame@@QAX@Z [HP1 0x103A3F60]
	static void NCompareGesture(UObject* Self, ScriptArray& InMousePoints, float fAccuracy, float& ReturnValue)
	{
		TypedScriptArray<vec3> mouse(&InMousePoints);
		int count = (int)std::min<size_t>(mouse.size(), 1024);
		std::vector<Point2> drawn;
		drawn.reserve(count);
		for (int i = 0; i < count; i++)
		{
			const vec3& p = mouse[i];
			if (p.z != -1.0f)
				drawn.push_back({ p.x, p.y });
		}
		ReturnValue = ScoreGesture(drawn, SampleTemplate(Self), fAccuracy);
	}

	// Distance (capped at 1) from the point to the nearest template sample. fAccuracy is read but unused.
	// IDA Engine.dll: ?execCompareGesturePoint@UGesture@@QAEXAAUFFrame@@QAX@Z [HP1 0x103A3CC0]
	static void NCompareGesturePoint(UObject* Self, const vec3& InMousePoint, float fAccuracy, float& ReturnValue)
	{
		ReturnValue = NearestDistance({ InMousePoint.x, InMousePoint.y }, SampleTemplate(Self));
	}

	void RegisterGestureNatives()
	{
		OverrideNative(426, [] { RegisterVMNativeFunc_3("Gesture", "CompareGesture", &NCompareGesture, 426); });
		OverrideNative(427, [] { RegisterVMNativeFunc_3("Gesture", "CompareGesturePoint", &NCompareGesturePoint, 427); });
	}
}
