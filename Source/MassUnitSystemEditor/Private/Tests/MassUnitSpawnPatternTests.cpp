// Copyright Digi Logic Labs LLC. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Templates/UnrealTemplate.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/MassUnitSpawner.h"
#include "Kismet/GameplayStatics.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Tests/AutomationCommon.h"

namespace MassUnitSpawnPatternTests
{
	AMassUnitSpawner* MakeSpawner(UWorld* World)
	{
		AMassUnitSpawner* Spawner = World->SpawnActorDeferred<AMassUnitSpawner>(
			AMassUnitSpawner::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Spawner)
		{
			Spawner->bSpawnOnBeginPlay = false;
			Spawner->bMoveOnBeginPlay = false;
			Spawner->bSpawnOnAuthorityOnly = false;
			UGameplayStatics::FinishSpawningActor(Spawner, FTransform::Identity);
		}
		return Spawner;
	}

	float MinPairDistance2D(const TArray<FVector>& Points)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 A = 0; A < Points.Num(); ++A)
		{
			for (int32 B = A + 1; B < Points.Num(); ++B)
			{
				Best = FMath::Min(Best, FVector::Dist2D(Points[A], Points[B]));
			}
		}
		return Best;
	}

	float MaxRadius2D(const TArray<FVector>& Points)
	{
		float Best = 0.0f;
		for (const FVector& Point : Points)
		{
			Best = FMath::Max(Best, FVector2D(Point.X, Point.Y).Size());
		}
		return Best;
	}

	float VariantSeedOf(const FMassEntityManager& EntityManager, const FMassUnitHandle& Unit)
	{
		const FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(Unit.EntityHandle.ToMassEntityHandle());
		return Visual ? Visual->VariantSeed : -1.0f;
	}

	int32 TeamOf(const FMassEntityManager& EntityManager, const FMassUnitHandle& Unit)
	{
		const FMassUnitTeamFragment* Team = EntityManager.GetFragmentDataPtr<FMassUnitTeamFragment>(Unit.EntityHandle.ToMassEntityHandle());
		return Team ? Team->TeamID : INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitSpawnPatternLayoutTest,
	"MassUnitSystem.Spawner.PatternLayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitSpawnPatternLayoutTest::RunTest(const FString& Parameters)
{
	using namespace MassUnitSpawnPatternTests;

	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("A transient game world can be created"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	AMassUnitSpawner* Spawner = MakeSpawner(TestWorld.GetTestWorld());
	if (!TestNotNull(TEXT("Spawner can be created"), Spawner))
	{
		return false;
	}
	Spawner->UnitSpacing = FVector2D(150.0f, 150.0f);
	Spawner->SpawnHeight = 50.0f;

	// Grid is the default and must keep the original centred-rows layout exactly.
	TestEqual(TEXT("Grid is the default pattern"), Spawner->SpawnPattern, EMassUnitSpawnPattern::Grid);
	Spawner->Columns = 3;
	const TArray<FVector> Grid = Spawner->CalculateLocalSpawnOffsets(5);
	TestEqual(TEXT("Grid returns one point per unit"), Grid.Num(), 5);
	if (Grid.Num() == 5)
	{
		// 2 rows (3 + 2): rows at X = -75 / +75; first row Y = -150, 0, 150; second row Y = -75, 75.
		TestTrue(TEXT("Grid unit 0 is unchanged"), Grid[0].Equals(FVector(-75.0f, -150.0f, 50.0f)));
		TestTrue(TEXT("Grid unit 2 is unchanged"), Grid[2].Equals(FVector(-75.0f, 150.0f, 50.0f)));
		TestTrue(TEXT("Grid short last row stays centred"), Grid[3].Equals(FVector(75.0f, -75.0f, 50.0f)));
	}
	TestEqual(TEXT("Grid has no pattern radius"), Spawner->GetPatternRadius(5), 0.0f);

	// Disc: sunflower spiral, even spacing, every unit inside the reported radius.
	Spawner->SpawnPattern = EMassUnitSpawnPattern::Disc;
	const TArray<FVector> Disc = Spawner->CalculateLocalSpawnOffsets(200);
	TestEqual(TEXT("Disc returns one point per unit"), Disc.Num(), 200);
	TestTrue(TEXT("Disc units stay inside the reported radius"), MaxRadius2D(Disc) <= Spawner->GetPatternRadius(200) + 1.0f);
	TestTrue(TEXT("Disc units never stack (nearest pair >= half the spacing)"), MinPairDistance2D(Disc) >= 75.0f);
	TestTrue(TEXT("Disc keeps SpawnHeight"), Disc.Num() > 0 && FMath::IsNearlyEqual(Disc[0].Z, 50.0f));

	// Ring: everyone on the circle; the auto radius leaves about one spacing between neighbours.
	Spawner->SpawnPattern = EMassUnitSpawnPattern::Ring;
	Spawner->PatternRadius = 0.0f;
	const TArray<FVector> Ring = Spawner->CalculateLocalSpawnOffsets(40);
	const float AutoRingRadius = Spawner->GetPatternRadius(40);
	bool bAllOnRing = Ring.Num() == 40;
	for (const FVector& Point : Ring)
	{
		bAllOnRing &= FMath::IsNearlyEqual(FVector2D(Point.X, Point.Y).Size(), AutoRingRadius, 0.5f);
	}
	TestTrue(TEXT("Ring units all sit on the ring"), bAllOnRing);
	TestTrue(TEXT("Auto ring spacing is close to UnitSpacing"), FMath::IsNearlyEqual(MinPairDistance2D(Ring), 150.0f, 5.0f));
	Spawner->PatternRadius = 2000.0f;
	TestEqual(TEXT("An explicit ring radius wins"), Spawner->GetPatternRadius(40), 2000.0f);

	// Line: centred on the actor along its right axis.
	Spawner->SpawnPattern = EMassUnitSpawnPattern::Line;
	const TArray<FVector> Line = Spawner->CalculateLocalSpawnOffsets(4);
	TestTrue(TEXT("Line is centred and UnitSpacing.Y apart"),
		Line.Num() == 4 && Line[0].Equals(FVector(0.0f, -225.0f, 50.0f)) && Line[3].Equals(FVector(0.0f, 225.0f, 50.0f)));

	// Scatter: same seed = same layout, other seed = other layout, all inside the radius.
	Spawner->SpawnPattern = EMassUnitSpawnPattern::Scatter;
	Spawner->PatternRadius = 1000.0f;
	Spawner->SpawnOptions.RandomSeed = 1234;
	const TArray<FVector> ScatterA = Spawner->CalculateLocalSpawnOffsets(100);
	const TArray<FVector> ScatterB = Spawner->CalculateLocalSpawnOffsets(100);
	Spawner->SpawnOptions.RandomSeed = 4321;
	const TArray<FVector> ScatterC = Spawner->CalculateLocalSpawnOffsets(100);
	TestTrue(TEXT("Scatter is deterministic for a seed"), ScatterA == ScatterB);
	TestFalse(TEXT("A different seed gives a different scatter"), ScatterA == ScatterC);
	TestTrue(TEXT("Scatter stays inside PatternRadius"), MaxRadius2D(ScatterA) <= 1000.0f + 0.5f);

	TestEqual(TEXT("Zero units give no points"), Spawner->CalculateLocalSpawnOffsets(0).Num(), 0);

	// The static helpers (used by the stress director without a spawner) give the spawner's exact layouts.
	const EMassUnitSpawnPattern AllPatterns[] = {
		EMassUnitSpawnPattern::Grid, EMassUnitSpawnPattern::Disc, EMassUnitSpawnPattern::Ring,
		EMassUnitSpawnPattern::Line, EMassUnitSpawnPattern::Scatter };
	for (const EMassUnitSpawnPattern Pattern : AllPatterns)
	{
		Spawner->SpawnPattern = Pattern;
		const TArray<FVector> FromSpawner = Spawner->CalculateLocalSpawnOffsets(37);
		const TArray<FVector> FromStatic = AMassUnitSpawner::ComputePatternOffsets(
			Pattern, 37, Spawner->Columns, Spawner->UnitSpacing, Spawner->PatternRadius,
			Spawner->SpawnOptions.RandomSeed, Spawner->SpawnHeight);
		TestTrue(*FString::Printf(TEXT("Static offsets match the spawner for pattern %d"), static_cast<int32>(Pattern)),
			FromSpawner == FromStatic);
		TestEqual(*FString::Printf(TEXT("Static radius matches the spawner for pattern %d"), static_cast<int32>(Pattern)),
			AMassUnitSpawner::ComputePatternRadius(Pattern, 37, Spawner->UnitSpacing, Spawner->PatternRadius),
			Spawner->GetPatternRadius(37));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitBatchSpawnTest,
	"MassUnitSystem.Spawner.BatchSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitBatchSpawnTest::RunTest(const FString& Parameters)
{
	using namespace MassUnitSpawnPatternTests;

	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("A transient game world can be created"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UMassUnitSubsystem* UnitSubsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	UMassEntitySubsystem* MassSubsystem = UnitSubsystem ? UnitSubsystem->GetEntitySubsystem() : nullptr;
	if (!TestNotNull(TEXT("Unit manager is available"), UnitManager) || !TestNotNull(TEXT("Mass subsystem is available"), MassSubsystem))
	{
		return false;
	}
	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();

	TArray<FTransform> Transforms;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Transforms.Emplace(FVector(Index * 200.0f, 0.0f, 50.0f));
	}

	// Default options: same units as single creation (template team, index-derived variant seed).
	const TArray<FMassUnitHandle> Plain = UnitManager->CreateUnitsFromTemplate(nullptr, Transforms, FMassUnitBatchSpawnOptions());
	TestEqual(TEXT("A null template batch creates every unit"), Plain.Num(), 8);
	bool bIndexSeeds = Plain.Num() == 8;
	for (const FMassUnitHandle& Unit : Plain)
	{
		const float Expected = FMath::Frac(static_cast<float>(Unit.EntityHandle.ToMassEntityHandle().Index) * 0.6180339887f);
		bIndexSeeds &= FMath::IsNearlyEqual(VariantSeedOf(EntityManager, Unit), Expected);
	}
	TestTrue(TEXT("Default options keep the index-derived variant seed"), bIndexSeeds);
	FTransform FirstTransform;
	TestTrue(TEXT("Batch units take their own transforms"),
		Plain.Num() == 8 && UnitManager->GetUnitTransform(Plain[3], FirstTransform)
		&& FirstTransform.GetLocation().Equals(Transforms[3].GetLocation()));
	for (const FMassUnitHandle& Unit : Plain)
	{
		UnitManager->DestroyUnit(Unit);
	}

	// Team override and seeded variants.
	FMassUnitBatchSpawnOptions Options;
	Options.bOverrideTeam = true;
	Options.TeamID = 7;
	Options.RandomSeed = 99;
	Options.bSeedVariants = true;
	const TArray<FMassUnitHandle> TeamA = UnitManager->CreateUnitsFromTemplate(nullptr, Transforms, Options);
	TestEqual(TEXT("Team override reaches every unit"), UnitManager->GetUnitsByTeam(7).Num(), 8);
	bool bAllTeam7 = TeamA.Num() == 8;
	for (const FMassUnitHandle& Unit : TeamA)
	{
		bAllTeam7 &= TeamOf(EntityManager, Unit) == 7;
	}
	TestTrue(TEXT("Team fragments carry the override"), bAllTeam7);
	TArray<float> SeedsA;
	for (const FMassUnitHandle& Unit : TeamA)
	{
		SeedsA.Add(VariantSeedOf(EntityManager, Unit));
		UnitManager->DestroyUnit(Unit);
	}
	TestEqual(TEXT("Destroyed batch units leave the team index"), UnitManager->GetUnitsByTeam(7).Num(), 0);

	// Same seed again: same variant seeds, whatever entity indices Mass hands out this time.
	const TArray<FMassUnitHandle> TeamB = UnitManager->CreateUnitsFromTemplate(nullptr, Transforms, Options);
	TArray<float> SeedsB;
	for (const FMassUnitHandle& Unit : TeamB)
	{
		SeedsB.Add(VariantSeedOf(EntityManager, Unit));
		UnitManager->DestroyUnit(Unit);
	}
	TestTrue(TEXT("Seeded variants repeat for the same seed"), SeedsA == SeedsB && SeedsA.Num() == 8);

	// The unit limit clips the batch instead of failing it.
	UMassUnitSystemSettings* MutableSettings = GetMutableDefault<UMassUnitSystemSettings>();
	{
		TGuardValue<int32> MaxUnitsGuard(MutableSettings->MaxUnits, 5);
		const TArray<FMassUnitHandle> Clipped = UnitManager->CreateUnitsFromTemplate(nullptr, Transforms, FMassUnitBatchSpawnOptions());
		TestEqual(TEXT("MaxUnits clips the batch"), Clipped.Num(), 5);
		for (const FMassUnitHandle& Unit : Clipped)
		{
			UnitManager->DestroyUnit(Unit);
		}
	}
	TestEqual(TEXT("Empty transform list creates nothing"),
		UnitManager->CreateUnitsFromTemplate(nullptr, TArray<FTransform>(), FMassUnitBatchSpawnOptions()).Num(), 0);

	// The spawner uses the batch: pattern + team end to end.
	AMassUnitSpawner* Spawner = MakeSpawner(World);
	if (!TestNotNull(TEXT("Spawner can be created"), Spawner))
	{
		return false;
	}
	Spawner->UnitCount = 30;
	Spawner->SpawnPattern = EMassUnitSpawnPattern::Disc;
	Spawner->SpawnOptions.bOverrideTeam = true;
	Spawner->SpawnOptions.TeamID = 3;
	const TArray<FMassUnitHandle> Spawned = Spawner->SpawnUnits();
	TestEqual(TEXT("Patterned spawner creates the requested count"), Spawned.Num(), 30);
	TestEqual(TEXT("Patterned spawner tracks its units"), Spawner->GetValidSpawnedUnitCount(), 30);
	TestEqual(TEXT("Patterned spawner applies its team"), UnitManager->GetUnitsByTeam(3).Num(), 30);
	Spawner->DestroySpawnedUnits();
	TestEqual(TEXT("Spawner cleanup leaves no units"), UnitManager->GetUnitCount(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
