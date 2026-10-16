// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Entity/MassUnitVisibilityProcessor.h"
#include "Core/MassUnitStats.h"

#include "Config/MassUnitSystemSettings.h"
#include "Engine/World.h"
#include "Entity/MassUnitFragments.h"
#include "GameFramework/PlayerController.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"

UMassUnitVisibilityProcessor::UMassUnitVisibilityProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("MassUnitSystem.Visibility"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("MassUnitSystem.Combat")));
}

void UMassUnitVisibilityProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassUnitTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassUnitVisualFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitLODFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitVisualizationLODFragment>(EMassFragmentAccess::ReadWrite);
}

DECLARE_CYCLE_STAT(TEXT("Visibility / LOD Processor"), STAT_MassUnit_VisibilityProcessor, STATGROUP_MassUnitSystem);

void UMassUnitVisibilityProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_VisibilityProcessor);
	UWorld* World = EntityManager.GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	TArray<FVector> ViewLocations;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Controller = It->Get(); Controller && Controller->IsLocalController())
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
			ViewLocations.Add(ViewLocation);
		}
	}
	if (ViewLocations.IsEmpty())
	{
		return;
	}

	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	TArray<float> Thresholds = Settings ? Settings->LODDistanceThresholds : TArray<float>{500.0f, 1500.0f, 3000.0f, 6000.0f};
	Thresholds.Sort();
	TArray<float> UpdateIntervals = Settings
		? Settings->VisibilityLODUpdateIntervals
		: TArray<float>{0.05f, 0.1f, 0.2f, 0.5f, 1.0f};
	if (UpdateIntervals.IsEmpty())
	{
		UpdateIntervals.Add(0.1f);
	}
	const float SkeletalDistance = Settings ? Settings->SkeletalMeshDistance : 300.0f;
	const float SkeletalHysteresis = Settings ? FMath::Max(1.0f, Settings->SkeletalMeshHysteresis) : 1.2f;
	const float MaxVisibleDistance = Settings ? Settings->MaxVisibleDistance : 10000.0f;
	const float CurrentTime = World->GetTimeSeconds();

	EntityQuery.ForEachEntityChunk(Context, [ViewLocations, Thresholds, UpdateIntervals, SkeletalDistance, SkeletalHysteresis, MaxVisibleDistance, CurrentTime](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassUnitTransformFragment> Transforms = ChunkContext.GetFragmentView<FMassUnitTransformFragment>();
		TArrayView<FMassUnitVisualFragment> Visuals = ChunkContext.GetMutableFragmentView<FMassUnitVisualFragment>();
		TArrayView<FMassUnitLODFragment> LODs = ChunkContext.GetMutableFragmentView<FMassUnitLODFragment>();
		TArrayView<FMassUnitVisualizationLODFragment> VisualizationLODs = ChunkContext.GetMutableFragmentView<FMassUnitVisualizationLODFragment>();

		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			FMassUnitLODFragment& LOD = LODs[It];
			if (CurrentTime < LOD.NextUpdateTime)
			{
				continue;
			}

			float DistanceSquared = TNumericLimits<float>::Max();
			for (const FVector& ViewLocation : ViewLocations)
			{
				DistanceSquared = FMath::Min(
					DistanceSquared,
					FVector::DistSquared(Transforms[It].GetTransform().GetLocation(), ViewLocation));
			}
			int32 LODLevel = Thresholds.Num();
			for (int32 Index = 0; Index < Thresholds.Num(); ++Index)
			{
				if (DistanceSquared <= FMath::Square(FMath::Max(0.0f, Thresholds[Index])))
				{
					LODLevel = Index;
					break;
				}
			}

			FMassUnitVisualFragment& Visual = Visuals[It];
			Visual.ViewerDistanceSquared = DistanceSquared;
			Visual.LODLevel = LODLevel;
			Visual.bIsVisible = MaxVisibleDistance <= 0.0f || DistanceSquared <= FMath::Square(MaxVisibleDistance);
			const float SkeletalThreshold = Visual.bWantsSkeletalMesh
				? SkeletalDistance * SkeletalHysteresis
				: SkeletalDistance;
			Visual.bWantsSkeletalMesh = Visual.bIsVisible && Visual.SkeletalMesh != nullptr
				&& DistanceSquared <= FMath::Square(SkeletalThreshold);
			LOD.Level = LODLevel;
			const int32 IntervalIndex = FMath::Clamp(LODLevel, 0, UpdateIntervals.Num() - 1);
			const float BaseInterval = UpdateIntervals.IsValidIndex(IntervalIndex)
				? FMath::Max(0.0f, UpdateIntervals[IntervalIndex])
				: 0.1f;
			const FMassEntityHandle Entity = ChunkContext.GetEntity(It);
			const float UpdateJitter = 0.85f + (static_cast<float>(Entity.Index % 31) / 30.0f) * 0.3f;
			LOD.NextUpdateTime = CurrentTime + BaseInterval * UpdateJitter;
			VisualizationLODs[It].LODLevel = LODLevel;
		}
	});
}
