// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Entity/MassUnitSpawner.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Core/MassUnitSubsystem.h"
#include "Core/MassUnitSystemRuntime.h"
#include "DrawDebugHelpers.h"
#include "Entity/UnitTemplate.h"
#include "Navigation/MassUnitNavigationSystem.h"

AMassUnitSpawner::AMassUnitSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	DirectionArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("DirectionArrow"));
	DirectionArrow->SetupAttachment(SceneRoot);
	DirectionArrow->ArrowColor = FColor::Cyan;
	DirectionArrow->ArrowSize = 2.0f;
	DirectionArrow->bIsScreenSizeScaled = true;
}

void AMassUnitSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (!bSpawnOnBeginPlay || (bSpawnOnAuthorityOnly && GetNetMode() == NM_Client))
	{
		return;
	}

	SpawnUnits();
	if (bEnableCrowdSimulation)
	{
		StartCrowdSimulation();
	}
	else if (bMoveOnBeginPlay)
	{
		MoveSpawnedUnitsByOffset(DestinationOffset);
	}
}

void AMassUnitSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bDestroySpawnedUnitsOnEndPlay)
	{
		DestroySpawnedUnits();
	}
	Super::EndPlay(EndPlayReason);
}

TArray<FMassUnitHandle> AMassUnitSpawner::SpawnUnits()
{
	TArray<FMassUnitHandle> NewUnits;
	if (bSpawnOnAuthorityOnly && GetNetMode() == NM_Client)
	{
		UE_LOG(LogMassUnitSystem, Verbose, TEXT("%s skipped Mass unit spawning on a non-authority world"), *GetName());
		return NewUnits;
	}

	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	if (!UnitManager)
	{
		UE_LOG(LogMassUnitSystem, Warning, TEXT("%s could not resolve the Mass Unit world subsystem"), *GetName());
		return NewUnits;
	}

	const int32 RequestedCount = FMath::Max(0, UnitCount);
	const TArray<FVector> LocalOffsets = CalculateLocalSpawnOffsets(RequestedCount);
	TArray<FTransform> SpawnTransforms;
	SpawnTransforms.Reserve(LocalOffsets.Num());
	for (const FVector& LocalOffset : LocalOffsets)
	{
		SpawnTransforms.Emplace(GetActorQuat(), GetActorTransform().TransformPosition(LocalOffset), GetActorScale3D());
	}

	// One batch call; a null template gives the asset-free default unit, as before.
	NewUnits = UnitManager->CreateUnitsFromTemplate(UnitTemplate, SpawnTransforms, SpawnOptions);
	SpawnedUnits.Append(NewUnits);
	for (int32 Index = 0; Index < NewUnits.Num(); ++Index)
	{
		DrawSpawnDebug(SpawnTransforms[Index].GetLocation());
	}
	DrawPatternDebug(RequestedCount);

	UE_LOG(LogMassUnitSystem, Log, TEXT("%s spawned %d Mass units"), *GetName(), NewUnits.Num());
	return NewUnits;
}

int32 AMassUnitSpawner::MoveSpawnedUnitsByOffset(FVector LocalOffset)
{
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	if (!UnitManager)
	{
		return 0;
	}

	const FVector WorldOffset = GetActorTransform().TransformVectorNoScale(LocalOffset);
	int32 CommandedCount = 0;
	for (const FMassUnitHandle UnitHandle : SpawnedUnits)
	{
		FTransform CurrentTransform;
		if (!UnitManager->GetUnitTransform(UnitHandle, CurrentTransform))
		{
			continue;
		}
		const FVector Destination = CurrentTransform.GetLocation() + WorldOffset;
		if (CommandUnit(UnitHandle, Destination))
		{
			++CommandedCount;
			DrawCommandDebug(CurrentTransform.GetLocation(), Destination);
		}
	}
	return CommandedCount;
}

int32 AMassUnitSpawner::CommandSpawnedUnitsToLocation(FVector DestinationCenter, bool bPreserveSpacing)
{
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	if (!UnitManager)
	{
		return 0;
	}

	struct FUnitLocation
	{
		FMassUnitHandle Handle;
		FVector Location = FVector::ZeroVector;
	};
	TArray<FUnitLocation> UnitLocations;
	FVector CurrentCenter = FVector::ZeroVector;
	for (const FMassUnitHandle UnitHandle : SpawnedUnits)
	{
		FTransform CurrentTransform;
		if (UnitManager->GetUnitTransform(UnitHandle, CurrentTransform))
		{
			UnitLocations.Add({UnitHandle, CurrentTransform.GetLocation()});
			CurrentCenter += CurrentTransform.GetLocation();
		}
	}
	if (UnitLocations.IsEmpty())
	{
		return 0;
	}
	CurrentCenter /= static_cast<float>(UnitLocations.Num());

	int32 CommandedCount = 0;
	for (const FUnitLocation& Unit : UnitLocations)
	{
		const FVector Destination = bPreserveSpacing
			? DestinationCenter + (Unit.Location - CurrentCenter)
			: DestinationCenter;
		if (CommandUnit(Unit.Handle, Destination))
		{
			++CommandedCount;
			DrawCommandDebug(Unit.Location, Destination);
		}
	}
	return CommandedCount;
}

void AMassUnitSpawner::DestroySpawnedUnits()
{
	StopCrowdSimulation(true);
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	if (UnitManager)
	{
		for (const FMassUnitHandle UnitHandle : SpawnedUnits)
		{
			if (UnitManager->IsUnitValid(UnitHandle))
			{
				UnitManager->DestroyUnit(UnitHandle);
			}
		}
	}
	SpawnedUnits.Reset();
}

TArray<FMassUnitHandle> AMassUnitSpawner::GetValidSpawnedUnits() const
{
	TArray<FMassUnitHandle> ValidUnits;
	const UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	const UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	if (!UnitManager)
	{
		return ValidUnits;
	}
	for (const FMassUnitHandle UnitHandle : SpawnedUnits)
	{
		if (UnitManager->IsUnitValid(UnitHandle))
		{
			ValidUnits.Add(UnitHandle);
		}
	}
	return ValidUnits;
}

int32 AMassUnitSpawner::GetValidSpawnedUnitCount() const
{
	return GetValidSpawnedUnits().Num();
}

int32 AMassUnitSpawner::StartCrowdSimulation()
{
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	if (!CrowdSystem || (bSpawnOnAuthorityOnly && GetNetMode() == NM_Client))
	{
		return INDEX_NONE;
	}

	if (CrowdGroupHandle != INDEX_NONE)
	{
		CrowdSystem->UnregisterCrowdGroup(CrowdGroupHandle, false);
		CrowdGroupHandle = INDEX_NONE;
	}
	CrowdGroupHandle = CrowdSystem->RegisterCrowdGroup(
		GetValidSpawnedUnits(),
		GetActorLocation(),
		CrowdConfig,
		bUseNavigation,
		AcceptanceRadius);
	if (CrowdGroupHandle != INDEX_NONE && bEnablePlayerEngagement)
	{
		CrowdSystem->ConfigureCrowdGroupEngagement(CrowdGroupHandle, PlayerEngagementConfig, true);
		if (bEnableCollectiveBehavior)
		{
			CrowdSystem->ConfigureCrowdGroupCollectiveBehavior(CrowdGroupHandle, CollectiveConfig, true);
		}
		CrowdSystem->ForceCrowdGroupUpdate(CrowdGroupHandle);
	}
	return CrowdGroupHandle;
}

void AMassUnitSpawner::StopCrowdSimulation(bool bStopUnits)
{
	if (CrowdGroupHandle == INDEX_NONE)
	{
		return;
	}
	if (UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this))
	{
		if (UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem->GetCrowdSystem())
		{
			CrowdSystem->UnregisterCrowdGroup(CrowdGroupHandle, bStopUnits);
		}
	}
	CrowdGroupHandle = INDEX_NONE;
}

bool AMassUnitSpawner::ForceCrowdUpdate()
{
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	return CrowdSystem && CrowdSystem->ForceCrowdGroupUpdate(CrowdGroupHandle);
}

bool AMassUnitSpawner::IsCrowdSimulationActive() const
{
	const UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	const UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	return CrowdSystem && CrowdSystem->IsCrowdGroupRegistered(CrowdGroupHandle);
}

bool AMassUnitSpawner::ActivateCrowdAgainstActor(AActor* TargetActor)
{
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	return CrowdSystem && CrowdSystem->ActivateCrowdGroupForActor(CrowdGroupHandle, TargetActor);
}

bool AMassUnitSpawner::DeactivateCrowdEngagement(bool bReturnToWander)
{
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	return CrowdSystem && CrowdSystem->DeactivateCrowdGroupEngagement(CrowdGroupHandle, bReturnToWander);
}

bool AMassUnitSpawner::NotifySpawnedUnitInteracted(FMassUnitHandle UnitHandle, AActor* InteractingActor)
{
	if (!SpawnedUnits.Contains(UnitHandle))
	{
		return false;
	}
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	return CrowdSystem && CrowdSystem->NotifyUnitInteracted(UnitHandle, InteractingActor);
}

bool AMassUnitSpawner::DamageSpawnedUnitAndActivate(
	FMassUnitHandle UnitHandle,
	float Damage,
	AActor* DamageInstigator)
{
	if (!SpawnedUnits.Contains(UnitHandle))
	{
		return false;
	}
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	return CrowdSystem && CrowdSystem->DamageUnitAndActivate(UnitHandle, Damage, DamageInstigator);
}

FMassUnitHandle AMassUnitSpawner::FindClosestSpawnedUnit(
	FVector WorldLocation,
	float MaxDistance,
	bool bUse3DDistance) const
{
	const UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	const UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	if (!UnitManager || MaxDistance < 0.0f)
	{
		return {};
	}

	float BestDistanceSquared = FMath::Square(MaxDistance);
	FMassUnitHandle BestUnit;
	for (const FMassUnitHandle UnitHandle : SpawnedUnits)
	{
		FTransform Transform;
		FMassUnitStateFragment State;
		if (!UnitManager->GetUnitTransform(UnitHandle, Transform)
			|| !UnitManager->GetUnitState(UnitHandle, State)
			|| State.CurrentState == EMassUnitState::Dead)
		{
			continue;
		}
		const FVector Delta = Transform.GetLocation() - WorldLocation;
		const float DistanceSquared = bUse3DDistance ? Delta.SizeSquared() : Delta.SizeSquared2D();
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestUnit = UnitHandle;
		}
	}
	return BestUnit;
}

namespace MassUnitSpawnerPatterns
{
	/** Distance between neighbours in the round patterns: the tighter of the two grid spacings. */
	float RoundSpacing(const FVector2D& UnitSpacing)
	{
		return FMath::Max(1.0f, FMath::Min(UnitSpacing.X, UnitSpacing.Y));
	}

	/** Sunflower (Vogel) spiral constant: one unit per Spacing^2 of disc area. */
	float DiscScale(float Spacing)
	{
		return Spacing / FMath::Sqrt(UE_PI);
	}

	constexpr float GoldenAngle = 2.39996322972865332f; // pi * (3 - sqrt(5))
}

float AMassUnitSpawner::ComputePatternRadius(
	EMassUnitSpawnPattern Pattern,
	int32 TotalUnits,
	FVector2D InUnitSpacing,
	float InPatternRadius)
{
	using namespace MassUnitSpawnerPatterns;
	const int32 Count = FMath::Max(0, TotalUnits);
	const float Spacing = RoundSpacing(InUnitSpacing);
	switch (Pattern)
	{
	case EMassUnitSpawnPattern::Disc:
		return DiscScale(Spacing) * FMath::Sqrt(static_cast<float>(Count));
	case EMassUnitSpawnPattern::Ring:
		return InPatternRadius > 0.0f ? InPatternRadius : (Spacing * static_cast<float>(Count)) / (2.0f * UE_PI);
	case EMassUnitSpawnPattern::Scatter:
		return InPatternRadius > 0.0f ? InPatternRadius : DiscScale(Spacing) * FMath::Sqrt(static_cast<float>(Count));
	default:
		return 0.0f;
	}
}

TArray<FVector> AMassUnitSpawner::ComputePatternOffsets(
	EMassUnitSpawnPattern Pattern,
	int32 TotalUnits,
	int32 InColumns,
	FVector2D InUnitSpacing,
	float InPatternRadius,
	int32 RandomSeed,
	float Height)
{
	using namespace MassUnitSpawnerPatterns;
	const int32 Count = FMath::Max(0, TotalUnits);
	TArray<FVector> Offsets;
	Offsets.Reserve(Count);

	const float Radius = ComputePatternRadius(Pattern, Count, InUnitSpacing, InPatternRadius);
	const float Scale = DiscScale(RoundSpacing(InUnitSpacing));
	const int32 SafeColumns = FMath::Max(1, InColumns);
	const int32 RowCount = FMath::Max(1, FMath::DivideAndRoundUp(Count, SafeColumns));
	FRandomStream LayoutStream(RandomSeed);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FVector2D Local = FVector2D::ZeroVector;
		switch (Pattern)
		{
		case EMassUnitSpawnPattern::Disc:
		{
			// +0.5 keeps the first unit off the exact centre and the spacing even near it.
			const float Distance = Scale * FMath::Sqrt(static_cast<float>(Index) + 0.5f);
			const float Angle = static_cast<float>(Index) * GoldenAngle;
			Local = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
			break;
		}
		case EMassUnitSpawnPattern::Ring:
		{
			const float Angle = (2.0f * UE_PI * static_cast<float>(Index)) / static_cast<float>(Count);
			Local = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			break;
		}
		case EMassUnitSpawnPattern::Line:
			Local = FVector2D(0.0f, (static_cast<float>(Index) - static_cast<float>(Count - 1) * 0.5f) * InUnitSpacing.Y);
			break;
		case EMassUnitSpawnPattern::Scatter:
		{
			// sqrt keeps the density uniform over the disc instead of bunching at the centre.
			const float Distance = Radius * FMath::Sqrt(LayoutStream.FRand());
			const float Angle = 2.0f * UE_PI * LayoutStream.FRand();
			Local = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
			break;
		}
		case EMassUnitSpawnPattern::Grid:
		default:
		{
			const int32 RowIndex = Index / SafeColumns;
			const int32 ColumnIndex = Index % SafeColumns;
			const int32 UnitsInRow = FMath::Min(SafeColumns, Count - (RowIndex * SafeColumns));
			const float CenteredRow = static_cast<float>(RowIndex) - (static_cast<float>(RowCount - 1) * 0.5f);
			const float CenteredColumn = static_cast<float>(ColumnIndex) - (static_cast<float>(UnitsInRow - 1) * 0.5f);
			Local = FVector2D(CenteredRow * InUnitSpacing.X, CenteredColumn * InUnitSpacing.Y);
			break;
		}
		}
		Offsets.Add(FVector(Local.X, Local.Y, Height));
	}
	return Offsets;
}

float AMassUnitSpawner::GetPatternRadius(int32 TotalUnits) const
{
	return ComputePatternRadius(SpawnPattern, TotalUnits, UnitSpacing, PatternRadius);
}

TArray<FVector> AMassUnitSpawner::CalculateLocalSpawnOffsets(int32 TotalUnits) const
{
	return ComputePatternOffsets(
		SpawnPattern, TotalUnits, Columns, UnitSpacing, PatternRadius, SpawnOptions.RandomSeed, SpawnHeight);
}

bool AMassUnitSpawner::CommandUnit(FMassUnitHandle UnitHandle, const FVector& Destination) const
{
	UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(this);
	if (!UnitSubsystem)
	{
		return false;
	}
	if (bUseNavigation)
	{
		UMassUnitNavigationSystem* NavigationSystem = UnitSubsystem->GetNavigationSystem();
		return NavigationSystem && NavigationSystem->RequestPath(UnitHandle, Destination, AcceptanceRadius);
	}
	UMassUnitEntityManager* UnitManager = UnitSubsystem->GetUnitManager();
	return UnitManager && UnitManager->SetUnitDestination(UnitHandle, Destination, AcceptanceRadius);
}

void AMassUnitSpawner::DrawSpawnDebug(const FVector& Location) const
{
#if ENABLE_DRAW_DEBUG
	if (bEnableVisualDebug && GetWorld())
	{
		DrawDebugBox(GetWorld(), Location, FVector(45.0f), FColor::Cyan, false, DebugDuration, 0, 2.0f);
	}
#endif
}

void AMassUnitSpawner::DrawPatternDebug(int32 TotalUnits) const
{
#if ENABLE_DRAW_DEBUG
	const float Radius = GetPatternRadius(TotalUnits);
	if (bEnableVisualDebug && GetWorld() && Radius > 0.0f)
	{
		const FVector Centre = GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, SpawnHeight));
		DrawDebugCircle(GetWorld(), Centre, Radius, 64, FColor::Cyan, false, DebugDuration, 0, 3.0f,
			GetActorRightVector(), GetActorForwardVector(), false);
		DrawDebugString(GetWorld(), Centre + FVector(0.0f, 0.0f, 120.0f),
			FString::Printf(TEXT("%s x%d  r=%.0f  seed=%d"), *UEnum::GetDisplayValueAsText(SpawnPattern).ToString(),
				TotalUnits, Radius, SpawnOptions.RandomSeed),
			nullptr, FColor::White, DebugDuration, true);
	}
#endif
}

void AMassUnitSpawner::DrawCommandDebug(const FVector& Start, const FVector& Destination) const
{
#if ENABLE_DRAW_DEBUG
	if (bEnableVisualDebug && GetWorld())
	{
		DrawDebugDirectionalArrow(GetWorld(), Start, Destination, 75.0f, FColor::Green, false, DebugDuration, 0, 2.0f);
	}
#endif
}
