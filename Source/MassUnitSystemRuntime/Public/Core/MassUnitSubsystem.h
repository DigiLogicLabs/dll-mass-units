// Copyright Digi Logic Labs LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassUnitSubsystem.generated.h"

class UGASUnitIntegration;
class UFormationSystem;
class UMassEntitySubsystem;
class UMassUnitBehaviorIntegration;
class UMassUnitCommandSystem;
class UMassUnitCrowdSystem;
class UMassUnitEntityManager;
class UMassUnitNavigationSystem;
class UMassUnitStackSystem;
class UNiagaraUnitSystem;
class UUnitGameplayEventSystem;
class UUnitMeshPool;

/** World-scoped entry point for all Mass Unit System services. */
UCLASS()
class MASSUNITSYSTEMRUNTIME_API UMassUnitSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	/** Resolves the subsystem for a world-context object. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "Get Mass Unit Subsystem", Keywords = "Mass Entity Units"))
	static UMassUnitSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UMassUnitEntityManager* GetUnitManager() const { return UnitManager; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UFormationSystem* GetFormationSystem() const { return FormationSystem; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UMassUnitNavigationSystem* GetNavigationSystem() const { return NavigationSystem; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UNiagaraUnitSystem* GetNiagaraSystem() const { return NiagaraSystem; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UUnitMeshPool* GetMeshPool() const { return MeshPool; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UGASUnitIntegration* GetGASIntegration() const { return GASIntegration; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UMassUnitBehaviorIntegration* GetBehaviorIntegration() const { return BehaviorIntegration; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UMassUnitCrowdSystem* GetCrowdSystem() const { return CrowdSystem; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UUnitGameplayEventSystem* GetGameplayEventSystem() const { return GameplayEventSystem; }

	/** Stacking and piling (units climbing onto units). See UUnitTemplate::Stacking. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UMassUnitStackSystem* GetStackSystem() const { return StackSystem; }

	/** Orders for units (move in formation, stop, hold). See UMassUnitCommandSystem. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	UMassUnitCommandSystem* GetCommandSystem() const { return CommandSystem; }

	UMassEntitySubsystem* GetEntitySubsystem() const { return EntitySubsystem; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitEntityManager> UnitManager = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UFormationSystem> FormationSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitNavigationSystem> NavigationSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraUnitSystem> NiagaraSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UUnitMeshPool> MeshPool = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UGASUnitIntegration> GASIntegration = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitBehaviorIntegration> BehaviorIntegration = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitCrowdSystem> CrowdSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UUnitGameplayEventSystem> GameplayEventSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitStackSystem> StackSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitCommandSystem> CommandSystem = nullptr;
};
