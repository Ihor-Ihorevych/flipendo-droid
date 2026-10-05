#include "Precomp.h"
#include "KW.h"
#include "KWMove.h"
#include "KWActor.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Packages/Core/UClass.h"
#include "Package/PackageManager.h"
#include "Anim/KWAnimation.h"
#include "VM/NativeFunc.h"
#include "Math/coords.h"
#include "Engine.h"

// Things attached to bones: BonePos, PHYS_Trailer actors with AnimBone (Actor.AttachToOwner: torch fires, the broom
// trail, the caught Snitch), and the pawn's weapon (Harry's wand) at the mesh's WeaponBoneIndex.

namespace KW
{
	void OverrideNative(int index, void (*registerFunc)());

	// IDA Engine.dll: ?execBonePos@AActor@@QAEXAAUFFrame@@QAX@Z [HP1 0x1040A5F0]
	// Skeletal mesh: the bone's world position (a bone name that doesn't exist gives the mesh origin). Otherwise Location.
	static void NBonePos(UObject* Self, const NameString& Bone, vec3& ReturnValue)
	{
		UActor* actor = UObject::Cast<UActor>(Self);
		USkeletalMesh* mesh = UObject::TryCast<USkeletalMesh>(actor->Mesh());
		vec3 x, y, z;
		if (!mesh || !GetBoneCoords(actor, mesh, BoneIndex(mesh, Bone), ReturnValue, x, y, z))
			ReturnValue = actor->Location();
	}

	// physTrailer places the trailer with FarMoveActor(bNoCheck) and turns it with MoveActor (KnowWonder's movement
	// when its physics runs, else SurrealEngine's)
	static void TrailerPlace(UActor* actor, const vec3& location)
	{
		if (UseKWPhysics())
			FarMoveActor(actor, location, false, true);
		else
			actor->SetLocation(location);
	}

	static void TrailerTurn(UActor* actor, const Rotator& rotation)
	{
		CheckResult hit;
		if (UseKWPhysics())
			MoveActor(actor, vec3(0.0f), rotation, hit);
		else
			actor->SetRotation(rotation);
	}

	// IDA Engine.dll: ?physTrailer@AActor@@QAEXM@Z [HP1 0x103F5F80]
	void PhysTrailer(UActor* actor)
	{
		UActor* owner = actor->Owner();
		if (!owner)
			return;

		if (actor->DrawType() != DT_Sprite)
		{
			vec3 location = owner->Location();
			Rotator rotation;
			USkeletalMesh* skel = UObject::TryCast<USkeletalMesh>(owner->Mesh());
			vec3 x, y, z;
			if (AnimBone(actor) != 0 && owner->Mesh())
			{
				// UMesh::GetBoneCoords (non-skeletal) gives an origin without the actor's Location; no HP1 script
				// attaches to a bone of a vertex mesh, so those keep the owner's location and rotation.
				if (skel && GetBoneCoords(owner, skel, AnimBone(actor) - 1, location, x, y, z))
					rotation = OrthoRotation(x, y, z);
				else
					rotation = owner->Rotation();
			}
			else
			{
				if (actor->bTrailerSameRotation() || actor->bTrailerPrePivot())
				{
					Coords::Rotation(owner->Rotation()).GetAxes(x, y, z);
					vec3 p = actor->PrePivot();
					location += x * p.x + y * p.y + z * p.z;
				}
				if (actor->bTrailerSameRotation())
				{
					rotation = owner->Rotation();
				}
				else
				{
					vec3 v = owner->Velocity();
					if (std::abs(v.x) >= 0.0001f || std::abs(v.y) >= 0.0001f || std::abs(v.z) >= 0.0001f)
						rotation = Rotator::FromVector(-v);
					else
						rotation = Rotator(0x4000, 0, 0);
				}
			}
			TrailerPlace(actor, location);
			TrailerTurn(actor, rotation);
		}
		else if (actor->bTrailerPrePivot())
		{
			TrailerPlace(actor, owner->Location() + actor->PrePivot());
		}
		else if (actor->bTrailerSameRotation())
		{
			TrailerPlace(actor, owner->Location() - Coords::Rotation(owner->Rotation()).XAxis * actor->Mass());
		}
		else
		{
			TrailerPlace(actor, owner->Location());
		}
	}

	// IDA Render.dll: ?DrawActorSprite@URender@@QAEXPAUFSceneNode@@PAUFDynamicSprite@@@Z [HP1 Render 0x10B32850] (the pawn weapon block)
	// IDA Render.dll: ?DrawLodMesh@URender@@QAEXPAUFSceneNode@@PAUFDynamicSprite@@PAVAActor@@ABVFCoords@@K@Z [HP1 Render 0x10B0FF00] (takes the mesh's weapon coords when WeaponBoneIndex > -1)
	// After drawing a pawn: WeaponLoc/WeaponRot from the weapon frame, then the weapon's ThirdPersonMesh is drawn in that
	// frame (its Rotation is zeroed for the draw, so only the frame orients it).
	bool PawnWeaponFrame(UPawn* pawn, mat4& frameToWorld)
	{
		USkeletalMesh* mesh = UObject::TryCast<USkeletalMesh>(pawn->Mesh());
		vec3 origin, x, y, z;
		if (!mesh || !SkeletalWeaponFrame(pawn, mesh, origin, x, y, z))
			return false;

		static PropertyDataOffset weaponLoc, weaponRot;
		if (weaponLoc.DataOffset == ~(size_t)0)
		{
			UClass* cls = engine->packages->FindClass("Engine.Pawn");
			weaponLoc = cls->GetPropertyDataOffset("WeaponLoc");
			weaponRot = cls->GetPropertyDataOffset("WeaponRot");
		}
		pawn->Value<vec3>(weaponLoc) = origin;
		pawn->Value<Rotator>(weaponRot) = OrthoRotation(x, y, z);

		frameToWorld = mat4::identity();
		for (int i = 0; i < 3; i++)
		{
			frameToWorld.matrix[0 + i] = x[i];
			frameToWorld.matrix[4 + i] = y[i];
			frameToWorld.matrix[8 + i] = z[i];
			frameToWorld.matrix[12 + i] = origin[i];
		}
		return true;
	}

	void RegisterAttachNatives()
	{
		OverrideNative(257, [] { RegisterVMNativeFunc_2("Actor", "BonePos", &NBonePos, 257); });
	}
}
