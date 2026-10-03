#pragma once

#include "Packages/Engine/Actors/UActor.h"

// Accessors for HP1-only Actor properties that upstream's PropertyOffsets doesn't list.
// Offsets are looked up by name the first time they're needed.
namespace HP1
{
	struct ActorProps
	{
		PropertyDataOffset AuxAnims;
		PropertyDataOffset AnimBone;
		PropertyDataOffset TweenAlpha;
		PropertyDataOffset bAnimTransient;
		PropertyDataOffset bAnimMove;
	};
	const ActorProps& GetActorProps();

	inline TypedScriptArray<UActor*> AuxAnims(UActor* a) { return a->DynamicArray<UActor*>(GetActorProps().AuxAnims); }
	inline uint8_t& AnimBone(UActor* a) { return a->Value<uint8_t>(GetActorProps().AnimBone); }
	inline float& TweenAlpha(UActor* a) { return a->Value<float>(GetActorProps().TweenAlpha); }
	inline BitfieldBool bAnimTransient(UActor* a) { return a->BoolValue(GetActorProps().bAnimTransient); }
	inline BitfieldBool bAnimMove(UActor* a) { return a->BoolValue(GetActorProps().bAnimMove); }
}
