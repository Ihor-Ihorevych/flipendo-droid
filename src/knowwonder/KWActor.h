#pragma once

#include "Packages/Engine/Actors/UActor.h"

// Accessors for HP1-only Actor properties that upstream's PropertyOffsets doesn't list.
// Offsets are looked up by name the first time they're needed.
namespace KW
{
	struct ActorProps
	{
		PropertyDataOffset AuxAnims;
		PropertyDataOffset AnimBone;
		PropertyDataOffset TweenAlpha;
		PropertyDataOffset bAnimTransient;
		PropertyDataOffset bAnimMove;
		PropertyDataOffset Wideness;
		PropertyDataOffset CollideType;
		PropertyDataOffset bAlignBottom;
		PropertyDataOffset CollisionWidth;
		PropertyDataOffset SpecularGlow;
		PropertyDataOffset SpecularWidth;
		PropertyDataOffset LightRadiusInner;
		PropertyDataOffset LightSource;
		PropertyDataOffset bDarkLight;
		PropertyDataOffset Shadow;
		PropertyDataOffset Opacity;
		PropertyDataOffset VisibilityRadius;
		PropertyDataOffset VisibilityHeight;
	};
	const ActorProps& GetActorProps();

	// Actor.ECollideType
	enum ECollideType : uint8_t
	{
		CT_AlignedCylinder,
		CT_OrientedCylinder,
		CT_Box,
		CT_Shape
	};

	inline TypedScriptArray<UActor*> AuxAnims(UActor* a) { return a->DynamicArray<UActor*>(GetActorProps().AuxAnims); }
	inline uint8_t& AnimBone(UActor* a) { return a->Value<uint8_t>(GetActorProps().AnimBone); }
	inline float& TweenAlpha(UActor* a) { return a->Value<float>(GetActorProps().TweenAlpha); }
	inline BitfieldBool bAnimTransient(UActor* a) { return a->BoolValue(GetActorProps().bAnimTransient); }
	inline BitfieldBool bAnimMove(UActor* a) { return a->BoolValue(GetActorProps().bAnimMove); }
	inline uint8_t& Wideness(UActor* a) { return a->Value<uint8_t>(GetActorProps().Wideness); }
	inline uint8_t& CollideType(UActor* a) { return a->Value<uint8_t>(GetActorProps().CollideType); }
	inline BitfieldBool bAlignBottom(UActor* a) { return a->BoolValue(GetActorProps().bAlignBottom); }
	inline float& CollisionWidth(UActor* a) { return a->Value<float>(GetActorProps().CollisionWidth); }
	inline float& SpecularGlow(UActor* a) { return a->Value<float>(GetActorProps().SpecularGlow); }
	inline uint8_t& SpecularWidth(UActor* a) { return a->Value<uint8_t>(GetActorProps().SpecularWidth); }
	inline uint8_t& LightRadiusInner(UActor* a) { return a->Value<uint8_t>(GetActorProps().LightRadiusInner); }
	inline uint8_t& LightSource(UActor* a) { return a->Value<uint8_t>(GetActorProps().LightSource); }
	inline BitfieldBool bDarkLight(UActor* a) { return a->BoolValue(GetActorProps().bDarkLight); }
	inline UActor*& Shadow(UActor* a) { return a->Value<UActor*>(GetActorProps().Shadow); }
	inline float& Opacity(UActor* a) { return a->Value<float>(GetActorProps().Opacity); }
	inline float& VisibilityRadius(UActor* a) { return a->Value<float>(GetActorProps().VisibilityRadius); }
	inline float& VisibilityHeight(UActor* a) { return a->Value<float>(GetActorProps().VisibilityHeight); }
}
