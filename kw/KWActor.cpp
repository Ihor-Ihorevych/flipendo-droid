#include "Precomp.h"
#include "KWActor.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Engine.h"

namespace KW
{
	const ActorProps& GetActorProps()
	{
		static ActorProps props;
		static bool initialized = false;
		if (!initialized)
		{
			UClass* cls = engine->packages->FindClass("Engine.Actor");
			if (!cls)
				Exception::Throw("HP1: Engine.Actor class not found");
			props.AuxAnims = cls->GetPropertyDataOffset("AuxAnims");
			props.AnimBone = cls->GetPropertyDataOffset("AnimBone");
			props.TweenAlpha = cls->GetPropertyDataOffset("TweenAlpha");
			props.bAnimTransient = cls->GetPropertyDataOffset("bAnimTransient");
			props.bAnimMove = cls->GetPropertyDataOffset("bAnimMove");
			props.Wideness = cls->GetPropertyDataOffset("Wideness");
			props.CollideType = cls->GetPropertyDataOffset("CollideType");
			props.bAlignBottom = cls->GetPropertyDataOffset("bAlignBottom");
			props.CollisionWidth = cls->GetPropertyDataOffset("CollisionWidth");
			initialized = true;
		}
		return props;
	}

	UObject* ObjectProperty(UObject* obj, const char* name)
	{
		if (!obj || !obj->HasProperty(name))
			return nullptr;
		UObject** value = static_cast<UObject**>(obj->GetProperty(name));
		return value ? *value : nullptr;
	}

	bool BoolProperty(UObject* obj, const char* name)
	{
		return obj && obj->HasProperty(name) && obj->BoolValue(obj->GetPropertyDataOffset(name));
	}
}
