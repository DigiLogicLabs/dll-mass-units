// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Core/MassUnitSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "Materials/MaterialInterface.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UObjectIterator.h"
#include "Visual/MassUnitLookProfile.h"
#include "Visual/NiagaraUnitSystem.h"

/**
 * Looks (UMassUnitLookProfile): the roll is deterministic per seed byte (what an adopter replicates), weights and part
 * chances hold, scale and tint stay in their ranges; a template's profile dresses spawned units, a new seed re-rolls,
 * a batch override and a per-unit profile re-dress, clearing restores the template mesh; the instanced renderer draws
 * each part as its own instance with the body's transform and per-instance values.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitLookTest,
	"DLLMassUnits.Units.Looks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitLookTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Grid = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
	if (!TestTrue(TEXT("Engine basic shapes load"), Cube && Sphere && Cone && Cylinder && Grid))
	{
		return false;
	}

	UMassUnitLookProfile* Profile = NewObject<UMassUnitLookProfile>(GetTransientPackage());
	Profile->BodyVariants.Add({ Cube, nullptr, 1.0f });
	Profile->BodyVariants.Add({ Sphere, Grid, 1.0f });
	Profile->BodyVariants.Add({ Cylinder, nullptr, 0.0f });   // zero weight: never
	FMassUnitLookPart Hat;
	Hat.PartName = TEXT("Hat");
	Hat.Chance = 1.0f;
	Hat.Options.Add({ Cone, nullptr, 1.0f });
	Profile->Parts.Add(Hat);
	FMassUnitLookPart Never;
	Never.PartName = TEXT("Never");
	Never.Chance = 0.0f;
	Never.Options.Add({ Cylinder, nullptr, 1.0f });
	Profile->Parts.Add(Never);
	Profile->ScaleRange = FVector2D(0.8, 1.2);
	Profile->TintPalette = { FLinearColor::Red, FLinearColor::Green, FLinearColor::Blue };
	TestEqual(TEXT("Max part count ignores zero-chance parts"), Profile->GetMaxPartCount(), 1);

	// --- Roll -------------------------------------------------------------------------------------
	TSet<UStaticMesh*> Bodies;
	bool bInRange = true;
	bool bHatAlways = true;
	bool bMaterialFollowsVariant = true;
	TSet<FString> Tints;
	for (int32 SeedByte = 0; SeedByte <= 255; ++SeedByte)
	{
		const FMassUnitRolledLook Look = Profile->RollLook(SeedByte / 255.0f);
		Bodies.Add(Look.Body.Mesh);
		bInRange &= Look.Scale >= 0.8f - KINDA_SMALL_NUMBER && Look.Scale <= 1.2f + KINDA_SMALL_NUMBER;
		bHatAlways &= Look.Parts.Num() == 1 && Look.Parts[0].Mesh == Cone;
		bMaterialFollowsVariant &= (Look.Body.Mesh == Sphere) == (Look.Body.Material == Grid);
		Tints.Add(Look.Tint.ToString());
	}
	TestTrue(TEXT("Both weighted bodies come up across seeds"), Bodies.Contains(Cube) && Bodies.Contains(Sphere));
	TestFalse(TEXT("A zero-weight body never comes up"), Bodies.Contains(Cylinder));
	TestTrue(TEXT("Scale stays inside the range"), bInRange);
	TestTrue(TEXT("A chance-1 part always shows, a chance-0 part never"), bHatAlways);
	TestTrue(TEXT("A variant's material comes with its mesh"), bMaterialFollowsVariant);
	TestEqual(TEXT("Every palette colour is used"), Tints.Num(), 3);
	{
		const FMassUnitRolledLook A = Profile->RollLook(0.5f);
		const FMassUnitRolledLook B = Profile->RollLook(0.5f + 0.0015f);   // same seed byte
		TestTrue(TEXT("The same seed byte rolls the same look (server and clients agree)"),
			A.Body.Mesh == B.Body.Mesh && FMath::IsNearlyEqual(A.Scale, B.Scale) && A.Tint == B.Tint);
	}

	// --- Spawned units ----------------------------------------------------------------------------
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("A transient game world can be created"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UMassUnitSubsystem* UnitSubsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	if (!TestNotNull(TEXT("Unit manager is available"), UnitManager))
	{
		return false;
	}
	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->StaticMesh = Cylinder;
	Template->LookProfile = Profile;

	TArray<FMassUnitHandle> Units;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Units.Add(UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(Index * 150.0f, 0.0f, 100.0f))));
	}
	FMassUnitRolledLook Worn;
	TestTrue(TEXT("A spawned unit reports its look"), UnitManager->GetUnitLook(Units[0], Worn));
	TestTrue(TEXT("The template's profile dresses spawned units (rolled body, hat)"),
		(Worn.Body.Mesh == Cube || Worn.Body.Mesh == Sphere) && Worn.Parts.Num() == 1);
	TestEqual(TEXT("The unit knows its profile"), UnitManager->GetUnitLookProfile(Units[0]), Profile);
	{
		const float Seed = UnitManager->GetUnitVariantSeed(Units[0]);
		const FMassUnitRolledLook Expected = Profile->RollLook(Seed);
		TestTrue(TEXT("The worn look is the profile's roll of the unit's seed"),
			Worn.Body.Mesh == Expected.Body.Mesh && FMath::IsNearlyEqual(Worn.Scale, Expected.Scale) && Worn.Tint == Expected.Tint);
	}

	// A client puppet receives the server's seed: the look re-rolls to match.
	for (int32 SeedByte = 0; SeedByte <= 255; SeedByte += 17)
	{
		UnitManager->SetUnitVariantSeed(Units[1], SeedByte / 255.0f);
		UnitManager->GetUnitLook(Units[1], Worn);
		const FMassUnitRolledLook Expected = Profile->RollLook(SeedByte / 255.0f);
		if (!TestTrue(TEXT("Setting the seed re-rolls the look"), Worn.Body.Mesh == Expected.Body.Mesh && Worn.Tint == Expected.Tint))
		{
			break;
		}
	}

	// Per-unit re-dress and clear.
	UMassUnitLookProfile* Plain = NewObject<UMassUnitLookProfile>(GetTransientPackage());
	Plain->BodyVariants.Add({ Cone, nullptr, 1.0f });
	TestTrue(TEXT("A unit can be re-dressed"), UnitManager->SetUnitLookProfile(Units[2], Plain));
	UnitManager->GetUnitLook(Units[2], Worn);
	TestTrue(TEXT("Re-dressed: the new body, no parts, no scale or tint"),
		Worn.Body.Mesh == Cone && Worn.Parts.IsEmpty() && FMath::IsNearlyEqual(Worn.Scale, 1.0f) && Worn.Tint == FLinearColor::White);
	UnitManager->SetUnitLookProfile(Units[3], nullptr);
	UnitManager->GetUnitLook(Units[3], Worn);
	TestTrue(TEXT("Clearing the profile restores the template mesh"), Worn.Body.Mesh == Cylinder && Worn.Parts.IsEmpty());

	// Batch override: same template, another look.
	FMassUnitBatchSpawnOptions Options;
	Options.LookProfile = Plain;
	const TArray<FMassUnitHandle> Batch = UnitManager->CreateUnitsFromTemplate(Template, { FTransform(FVector(0.0f, 600.0f, 100.0f)), FTransform(FVector(150.0f, 600.0f, 100.0f)) }, Options);
	TestEqual(TEXT("The batch spawned"), Batch.Num(), 2);
	if (Batch.Num() == 2)
	{
		UnitManager->GetUnitLook(Batch[0], Worn);
		TestTrue(TEXT("A batch look override replaces the template's profile"), Worn.Body.Mesh == Cone && Worn.Parts.IsEmpty());
	}

	// --- Renderer ---------------------------------------------------------------------------------
	UNiagaraUnitSystem* VisualSystem = UnitSubsystem->GetNiagaraSystem();
	if (VisualSystem && !VisualSystem->IsUsingNiagara())
	{
		TArray<FMassUnitHandle> Drawn = Units;
		Drawn.Append(Batch);
		VisualSystem->UpdateUnitVisualsByHandles(Drawn);
		int32 ExpectedInstances = 0;
		for (const FMassUnitHandle& Unit : Drawn)
		{
			UnitManager->GetUnitLook(Unit, Worn);
			ExpectedInstances += 1 + Worn.Parts.Num();
		}
		TestEqual(TEXT("Every body and every part is one instance"), VisualSystem->GetInstancedMeshInstanceCount(), ExpectedInstances);
		int32 HatInstances = 0;
		bool bGridOnSphere = true;
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
		{
			if (It->GetWorld() != World)
			{
				continue;
			}
			if (It->GetStaticMesh() == Cone && It->GetMaterial(0) != Grid)
			{
				HatInstances += It->GetInstanceCount();
			}
			if (It->GetStaticMesh() == Sphere)
			{
				bGridOnSphere &= It->GetMaterial(0) == Grid;
			}
		}
		TestTrue(TEXT("Hats are drawn as instances (plus the re-dressed cone bodies)"), HatInstances >= 4);
		TestTrue(TEXT("A variant's material override is on its component"), bGridOnSphere);
	}
	else
	{
		AddInfo(TEXT("The project renders units through Niagara; the instanced renderer checks are skipped."));
	}
	return true;
}

#endif
