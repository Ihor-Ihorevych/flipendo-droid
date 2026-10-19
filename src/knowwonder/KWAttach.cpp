#include "Precomp.h"
#include "KW.h"
#include "KWMove.h"
#include "KWActor.h"
#include "KWGame.h"
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

	// IDA Render.dll: ?DrawActorSprite@URender@@QAEXPAUFSceneNode@@PAUFDynamicSprite@@@Z [HP1 Render 0x10B32850] [HP2 Render 0x10B32EE0] (the pawn weapon block)
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
		frameToWorld = mat4::identity();
		for (int i = 0; i < 3; i++)
		{
			frameToWorld.matrix[0 + i] = x[i];
			frameToWorld.matrix[4 + i] = y[i];
			frameToWorld.matrix[8 + i] = z[i];
			frameToWorld.matrix[12 + i] = origin[i];
		}

		// The scripts put the wand's tip at WeaponLoc + (0, 0, length) rotated by WeaponRot (baseWand.GetWandEndPoint: Lumos, the
		// charge particles, the spells' start), so the rotator's Z axis has to be the wand's own direction. HP1's weapon frame is
		// built that way. In HP2 the weapon bone's Z points elsewhere (down, for Harry) and the wand model is turned to its
		// place by its own mesh transform, so the direction is taken from the model: from its origin to the corner of its bounds
		// farthest from it, through the transform the wand is drawn with.
		vec3 zAxis = z, xAxis = x, yAxis = y;
		if (IsHP2())
		{
			UWeapon* weapon = pawn->Weapon();
			UMesh* weaponMesh = weapon ? weapon->ThirdPersonMesh() : nullptr;
			if (weaponMesh)
			{
				mat4 m = frameToWorld * mat4::scale(weapon->ThirdPersonScale()) * weaponMesh->meshToObject;
				auto transform = [&](const vec3& v)
				{
					vec3 r;
					for (int k = 0; k < 3; k++)
						r[k] = m.matrix[0 + k] * v.x + m.matrix[4 + k] * v.y + m.matrix[8 + k] * v.z + m.matrix[12 + k];
					return r;
				};
				const BBox& bounds = weaponMesh->MeshBoundingBox;
				vec3 root = transform(vec3(0.0f));
				vec3 tip = root;
				float farthest = 0.0f;
				for (int corner = 0; corner < 8; corner++)
				{
					vec3 local((corner & 1) ? bounds.max.x : bounds.min.x, (corner & 2) ? bounds.max.y : bounds.min.y, (corner & 4) ? bounds.max.z : bounds.min.z);
					vec3 world = transform(local);
					vec3 d = world - root;
					float length = dot(d, d);
					if (length > farthest)
					{
						farthest = length;
						tip = world;
					}
				}
				vec3 d = tip - root;
				float length = std::sqrt(dot(d, d));
				if (length > 1e-3f)
				{
					zAxis = d * (1.0f / length);
					static bool reported = false;
					if (!reported)
					{
						reported = true;
						LogMessage("Wand tip: direction " + std::to_string(zAxis.x) + "," + std::to_string(zAxis.y) + "," + std::to_string(zAxis.z) + " length " + std::to_string(length) + " (the scripts use 20), frame Z " + std::to_string(z.x) + "," + std::to_string(z.y) + "," + std::to_string(z.z));
					}
					xAxis = x - zAxis * dot(x, zAxis);
					float xl = std::sqrt(dot(xAxis, xAxis));
					if (xl < 1e-3f)
					{
						xAxis = y - zAxis * dot(y, zAxis);
						xl = std::sqrt(dot(xAxis, xAxis));
					}
					xAxis = xAxis * (1.0f / xl);
					yAxis = cross(xAxis, zAxis);
				}
			}
		}
		pawn->Value<vec3>(weaponLoc) = origin;
		pawn->Value<Rotator>(weaponRot) = OrthoRotation(xAxis, yAxis, zAxis);
		return true;
	}

	void RegisterAttachNatives()
	{
		OverrideNative(257, [] { RegisterVMNativeFunc_2("Actor", "BonePos", &NBonePos, 257); });
	}
}
