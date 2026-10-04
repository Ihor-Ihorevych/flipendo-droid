#pragma once

#include "Math/vec.h"

class UActor;
class UZoneInfo;

// HP1's mesh lighting (Render.dll's light manager, used by URender::DrawLodMesh): which lights reach an actor, and
// the light of each vertex. docs/re/lighting.md.
namespace KW
{
	struct MeshLightInfo
	{
		vec3 Location;
		vec3 Direction;     // LD_Plane: the light's X axis
		uint8_t Source = 0; // ELightSource
		float Radius = 0.0f;
		float InvFalloff = 0.0f; // 1 / (Radius - inner radius)
		vec3 Color;              // colour * brightness * Level.Brightness * fade, negated for bDarkLight
		bool Dark = false;
	};

	struct MeshLighting
	{
		vec3 Ambient = vec3(0.0f);
		vec3 Unlit = vec3(0.0f); // the colour of PF_Unlit faces
		float Diffuse = 0.0f;    // 2 * ScaleGlow
		float Specular = 0.0f;
		float SpecularCutoff = 0.0f;
		bool ScaleGlowCurve = false; // ScaleGlow 0.7 bends the diffuse term
		vec3 ViewLocation = vec3(0.0f);
		MeshLightInfo Lights[16];
		int NumLights = 0;

		// The light of a world space vertex with a unit (or zero) normal.
		vec3 Light(const vec3& location, const vec3& normal) const;
	};

	// Sets up the lighting of a mesh actor: ambient from its zone, and the lights picked for lightActor (the actor
	// whose location and lights are used, e.g. the owner of a weapon).
	void SetupMeshLighting(MeshLighting& out, UActor* actor, UActor* lightActor, UZoneInfo* zone);

	// Engine.dll's FGetHSV: a hue/saturation/brightness colour (brightness bent by HP1's curve).
	vec3 GetHSV(uint8_t hue, uint8_t saturation, uint8_t brightness);
	// URender::GlobalLighting: a light's colour this frame; brightness (0..1 in) is changed by its LightType.
	vec3 GlobalLighting(UActor* light, float& brightness);
}
