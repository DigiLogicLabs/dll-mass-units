// Copyright Digi Logic Labs LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassEntityFallback.h"
#include "Entity/MassUnitFragments.h"
#include "GameplayTagContainer.h"
#include "Gameplay/MassUnitAttitudeProfile.h"
#include "MassArchetypeTypes.h"
#include "MassEntityTypes.h"
#include "MassUnitEntityManager.generated.h"

class UMassEntitySubsystem;
class UUnitTemplate;
class AActor;

/** Blueprint-safe wrapper around a world-owned Mass entity handle. */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	FMassUnitEntityHandle EntityHandle;

	FMassUnitHandle() = default;
	explicit FMassUnitHandle(const FMassUnitEntityHandle& InEntityHandle) : EntityHandle(InEntityHandle) {}
	explicit FMassUnitHandle(const FMassEntityHandle InEntityHandle) : EntityHandle(InEntityHandle) {}

	operator FMassUnitEntityHandle() const { return EntityHandle; }
	bool IsValid() const { return EntityHandle.IsValid(); }
	friend bool operator==(const FMassUnitHandle& A, const FMassUnitHandle& B) { return A.EntityHandle == B.EntityHandle; }
};

/**
 * Per-batch overrides for CreateUnitsFromTemplate. The defaults change nothing: every unit comes out exactly as
 * CreateUnitFromTemplate would make it (template team, index-derived variant seed).
 */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitBatchSpawnOptions
{
	GENERATED_BODY()

	/** Put every unit of the batch on TeamID instead of the template's team (e.g. two opposing crowds from one template). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Team")
	bool bOverrideTeam = false;

	/** Team for the whole batch. The template's TeamFaction tag is kept. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Team", meta = (EditCondition = "bOverrideTeam"))
	int32 TeamID = 0;

	/** Also replace the template's team colour (renderer custom data). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Team", meta = (EditCondition = "bOverrideTeam"))
	bool bOverrideTeamColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Team", meta = (EditCondition = "bOverrideTeam && bOverrideTeamColor"))
	FLinearColor TeamColor = FLinearColor::White;

	/**
	 * Seed for everything random in the batch. With Seed Variants it stamps the units' variant seeds, and the
	 * spawner's Scatter pattern uses it for positions. The same seed gives the same batch on every run.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Random")
	int32 RandomSeed = 0;

	/**
	 * Stamp each unit's 0-1 variant seed from RandomSeed, so a layout looks the same whatever entity indices Mass
	 * hands out. Off = the default index-derived seed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Random")
	bool bSeedVariants = false;

	/**
	 * Dress the whole batch in this look instead of the template's LookProfile (same minion, different look). An
	 * adopter that replicates units must send the override too (clients spawn puppets from the template).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Look")
	TObjectPtr<UMassUnitLookProfile> LookProfile = nullptr;

	/** Give the whole batch this temperament instead of the template's AttitudeProfile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Attitude")
	TObjectPtr<UMassUnitAttitudeProfile> AttitudeProfile = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FMassUnitHealthChangedSignature,
	FMassUnitHandle, UnitHandle,
	float, PreviousHealth,
	float, NewHealth,
	AActor*, InstigatorActor);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMassUnitDiedSignature,
	FMassUnitHandle, UnitHandle,
	AActor*, InstigatorActor);

/** A damaging status ticked. Bind it to route the damage through the project's damage pipeline. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FMassUnitStatusDamageTickSignature,
	FMassUnitHandle, UnitHandle,
	FGameplayTag, StatusTag,
	FGameplayTag, DamageTypeTag,
	float, Damage,
	AActor*, InstigatorActor);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMassUnitStatusChangedSignature,
	FMassUnitHandle, UnitHandle,
	FGameplayTag, StatusTag,
	bool, bAdded);

/** A unit was launched (knockback, kill fling). Velocity is after the template scale and speed cap. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMassUnitLaunchedSignature,
	FMassUnitHandle, UnitHandle,
	FVector, Velocity,
	bool, bDead);

/** A launched unit touched down (dust, impact sound). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMassUnitLandedSignature,
	FMassUnitHandle, UnitHandle,
	FVector, Location,
	bool, bDead);

/** World-scoped facade for creating and accessing plugin-owned Mass entities. */
UCLASS(BlueprintType)
class MASSUNITSYSTEMRUNTIME_API UMassUnitEntityManager : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UMassEntitySubsystem* InEntitySubsystem);
	void Deinitialize();

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System")
	FMassUnitHandle CreateUnitFromTemplate(UUnitTemplate* Template, const FTransform& SpawnTransform);

	/**
	 * Creates one unit per transform in one call (stress tests, waves, patterned spawners). Template assets are
	 * resolved once for the batch. Stops at the first unit that cannot be created (MaxUnits reached) and returns
	 * the units made so far. A null template uses the asset-free default, like CreateDefaultUnit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System", meta = (AutoCreateRefTerm = "Options", ReturnDisplayName = "New Units"))
	TArray<FMassUnitHandle> CreateUnitsFromTemplate(
		UUnitTemplate* Template,
		const TArray<FTransform>& SpawnTransforms,
		const FMassUnitBatchSpawnOptions& Options);

	/** Creates a unit with the plugin's built-in gameplay defaults and asset-free cube representation. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System", meta = (DisplayName = "Create Default Unit", Keywords = "quick start spawn cube"))
	FMassUnitHandle CreateDefaultUnit(const FTransform& SpawnTransform);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System")
	void DestroyUnit(FMassUnitHandle UnitHandle);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	bool IsUnitValid(FMassUnitHandle UnitHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	int32 GetUnitCount() const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	TArray<FMassUnitHandle> GetAllUnits() const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	TArray<FMassUnitHandle> GetUnitsByType(FGameplayTag UnitType) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	TArray<FMassUnitHandle> GetUnitsByTeam(int32 TeamID) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	bool GetUnitTransform(FMassUnitHandle UnitHandle, FTransform& OutTransform) const;

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System")
	bool SetUnitTransform(FMassUnitHandle UnitHandle, const FTransform& NewTransform);

	/** Assigns a direct movement destination. Use the navigation system when navmesh routing is desired. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System")
	bool SetUnitDestination(FMassUnitHandle UnitHandle, const FVector& Destination, float AcceptanceRadius = 50.0f);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System")
	bool SetUnitTarget(FMassUnitHandle UnitHandle, FMassUnitHandle TargetUnit, float Priority = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System")
	bool ClearUnitTarget(FMassUnitHandle UnitHandle);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System")
	bool GetUnitState(FMassUnitHandle UnitHandle, FMassUnitStateFragment& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Health")
	bool GetUnitHealth(FMassUnitHandle UnitHandle, float& OutHealth, float& OutMaxHealth) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Health")
	float GetUnitHealthPercent(FMassUnitHandle UnitHandle) const;

	/** Applies direct lightweight damage. GAS-backed damage can be routed through UGASUnitIntegration instead. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Health")
	bool ApplyDamage(FMassUnitHandle UnitHandle, float Damage, AActor* DamageInstigator = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Health")
	bool HealUnit(FMassUnitHandle UnitHandle, float Amount, AActor* HealInstigator = nullptr);

	/** Returns the closest managed unit inside Max Distance. Choose planar or full XYZ distance per query. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Queries")
	FMassUnitHandle FindClosestUnit(
		FVector WorldLocation,
		float MaxDistance,
		bool bUse3DDistance = false,
		bool bIncludeDead = false) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Queries")
	TArray<FMassUnitHandle> GetUnitsInRadius(
		FVector WorldLocation,
		float Radius,
		bool bUse3DDistance = false,
		bool bIncludeDead = false) const;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Health|Events")
	FMassUnitHealthChangedSignature OnUnitHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Health|Events")
	FMassUnitDiedSignature OnUnitDied;

	// ------------------------------------------------------------------------
	// Gameplay tags and statuses
	//
	// A unit has no ASC, so these are its stand-in: an owned-tag container that gameplay can query
	// like ASC->HasMatchingGameplayTag, and timed statuses that play the role of duration Gameplay
	// Effects (granted tag, periodic damage, slow, stun). Ticking costs nothing while no unit has a
	// timed status. All of it is server-side simulation state.
	// ------------------------------------------------------------------------

	/**
	 * Applies a timed status. Re-applying the same tag refreshes its duration (and adds a stack when
	 * the status stacks). Returns false for dead units and for statuses the unit is immune to.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Status")
	bool ApplyUnitStatus(FMassUnitHandle UnitHandle, const FMassUnitStatusEffect& Status, AActor* StatusInstigator = nullptr);

	/** Removes the status with exactly this tag. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Status")
	bool RemoveUnitStatus(FMassUnitHandle UnitHandle, FGameplayTag StatusTag);

	/** Removes every status whose tag matches any of these (hierarchically), e.g. Status.Debuff for a cleanse. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Status")
	int32 RemoveUnitStatusesMatching(FMassUnitHandle UnitHandle, FGameplayTagContainer StatusTags);

	/** Convenience stun: no movement or attacks, stun clip, for Duration seconds. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Status")
	bool StunUnit(FMassUnitHandle UnitHandle, float Duration, AActor* StunInstigator = nullptr);

	// --- Presentation replication (network-agnostic; the game owns the wire, see docs/crowd_replication_plan.md) ---

	/**
	 * Server side: the unit's coarse presentation for a replication record - its state and a 5-bit swing counter that
	 * changes on every attack swing. Returns false for an invalid unit.
	 */
	bool GetUnitPresentation(FMassUnitHandle UnitHandle, EMassUnitState& OutState, uint8& OutSwingCounter) const;

	/**
	 * Client puppet side: shows a replicated state. Attacking restarts the attack clip whenever SwingCounter changes;
	 * Stunned shows the stun clip and holds the unit; Idle/Moving/Defending/Interacting hand locomotion back to the
	 * movement processor (Walk/Run/Idle from the puppet's own velocity). Dead is ignored - health replication kills the
	 * puppet through the normal death path. Presentation only: no damage is queued, no status is applied, no event
	 * fires, so calling it on a real unit cannot fake gameplay.
	 */
	bool ApplyPresentationState(FMassUnitHandle UnitHandle, EMassUnitState NewState, uint8 SwingCounter);

	/** The unit's 0-1 variant seed (see FMassUnitVisualFragment::VariantSeed). Negative when the unit is invalid. */
	float GetUnitVariantSeed(FMassUnitHandle UnitHandle) const;

	/** Overrides the variant seed - clients set the server's value on puppets so every machine draws the same unit. */
	bool SetUnitVariantSeed(FMassUnitHandle UnitHandle, float Seed);

	/**
	 * Re-dresses a unit: rolls Profile from its variant seed (body variant, parts, scale, tint). Null = back to the
	 * template's mesh as is. Setting the seed re-rolls the current profile too.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Look")
	bool SetUnitLookProfile(FMassUnitHandle UnitHandle, UMassUnitLookProfile* Profile);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Look")
	UMassUnitLookProfile* GetUnitLookProfile(FMassUnitHandle UnitHandle) const;

	/** What the unit is wearing: body, parts, scale and tint. False for invalid units. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Look")
	bool GetUnitLook(FMassUnitHandle UnitHandle, FMassUnitRolledLook& OutLook) const;

	/**
	 * Gives a unit a temperament: rolls Profile from its variant seed and applies it to the template's speed and attack
	 * cooldown, animation tempo, courage and climbing. Null = back to the template's values. Setting the seed re-rolls.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Attitude")
	bool SetUnitAttitudeProfile(FMassUnitHandle UnitHandle, UMassUnitAttitudeProfile* Profile);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Attitude")
	UMassUnitAttitudeProfile* GetUnitAttitudeProfile(FMassUnitHandle UnitHandle) const;

	/** The unit's rolled temperament (template values when it has no profile). False for invalid units. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Attitude")
	bool GetUnitAttitude(FMassUnitHandle UnitHandle, FMassUnitRolledAttitude& OutAttitude) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Status")
	TArray<FMassUnitStatusEffect> GetUnitStatuses(FMassUnitHandle UnitHandle) const;

	/** Adds permanent tags (until removed), for example a buff an aura keeps refreshing or Faction.Recruited. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Tags")
	bool AddUnitTags(FMassUnitHandle UnitHandle, FGameplayTagContainer Tags);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Tags")
	bool RemoveUnitTags(FMassUnitHandle UnitHandle, FGameplayTagContainer Tags);

	/** Every tag the unit carries: template, loose and status tags. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Tags")
	bool GetUnitTags(FMassUnitHandle UnitHandle, FGameplayTagContainer& OutTags) const;

	/** Hierarchical match, like ASC->HasMatchingGameplayTag: Status matches a unit carrying Status.Burning. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Tags")
	bool UnitHasTag(FMassUnitHandle UnitHandle, FGameplayTag Tag) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Tags")
	bool UnitHasAnyTags(FMassUnitHandle UnitHandle, FGameplayTagContainer Tags) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Tags")
	bool UnitMatchesTagQuery(FMassUnitHandle UnitHandle, const FGameplayTagQuery& Query) const;

	/** Living units matching the query. Radius <= 0 searches the whole world. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Tags")
	TArray<FMassUnitHandle> FindUnitsMatchingTagQuery(const FGameplayTagQuery& Query, FVector WorldLocation, float Radius = 0.0f, bool bUse3DDistance = false) const;

	/** Damaging statuses ticked. When nothing is bound the plugin applies the damage itself. */
	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Status|Events")
	FMassUnitStatusDamageTickSignature OnUnitStatusDamageTick;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Status|Events")
	FMassUnitStatusChangedSignature OnUnitStatusChanged;

	// --------------------------------------------------------------------------------------------
	// LAUNCH - knockback, juggles, kill flings (cheap ballistic arcs; near kills may become ragdolls)
	// --------------------------------------------------------------------------------------------

	/**
	 * Throws a unit along Velocity (cm/s) under UMassUnitSystemSettings::LaunchGravity. A living unit is stunned
	 * (MassUnit.Status.Launched) for the flight plus KnockdownSeconds, faces where the hit came from, then gets up.
	 * A dead unit tumbles, bounces once and settles, and in the near band may become a pooled physics ragdoll.
	 * Re-launching an airborne unit replaces its velocity (juggling). Scaled by the template's
	 * LaunchVelocityScale; returns false when that is 0, launches are disabled, or MaxAirborneUnits is reached.
	 * Runs on whichever machine calls it: the server for gameplay, clients for their own cosmetic puppets.
	 * bApplyTemplateScale false = Velocity is already final (a client replaying the server's broadcast velocity).
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Launch")
	bool LaunchUnit(FMassUnitHandle UnitHandle, FVector Velocity, AActor* LaunchInstigator = nullptr, bool bApplyTemplateScale = true);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Launch")
	bool IsUnitAirborne(FMassUnitHandle UnitHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Launch")
	int32 GetAirborneUnitCount() const { return ActiveLaunches.Num(); }

	/**
	 * Lowers an airborne unit's landing height to at most MaxLandZ, but only when its landing trace found no ground
	 * (trace skipped by budget, tracing off, nothing hit): such a unit would otherwise land at its launch height. A real
	 * ground hit (a step or slope above MaxLandZ) is kept. For units thrown from above the ground (falling off a pile).
	 * Returns false when the unit is not airborne.
	 */
	bool CapLaunchLandingHeight(FMassUnitHandle UnitHandle, float MaxLandZ);

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Launch|Events")
	FMassUnitLaunchedSignature OnUnitLaunched;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Launch|Events")
	FMassUnitLandedSignature OnUnitLanded;

	/** Integrates every airborne unit (one sparse loop). Called by the subsystem each frame; free with none airborne. */
	void TickLaunches(float DeltaTime);

	/** Dead units launched since the last call, for the mesh pool's near-band ragdolls. */
	TArray<TPair<FMassUnitEntityHandle, FVector>> ConsumeRagdollRequests();

	/** Advances timed statuses. Called by the subsystem; self-throttled and free while no unit has a timed status. */
	void TickStatuses(float CurrentTime);

	/** Units carrying at least one status; diagnostics. */
	int32 GetUnitsWithStatusCount() const { return UnitsWithStatus.Num(); }

	FMassUnitEntityHandle CreateUnitFromTemplateInternal(UUnitTemplate* Template, const FTransform& SpawnTransform);
	void DestroyUnitInternal(FMassUnitEntityHandle EntityHandle);
	const TArray<FMassUnitEntityHandle>& GetAllUnitsInternal() const { return AllUnits; }
	TArray<FMassUnitEntityHandle> GetUnitsByTypeInternal(FGameplayTag UnitType) const;
	TArray<FMassUnitEntityHandle> GetUnitsByTeamInternal(int32 TeamID) const;
	bool ApplyDamageInternal(
		FMassUnitEntityHandle EntityHandle,
		float Damage,
		AActor* DamageInstigator = nullptr,
		bool bDeferEvents = false);
	void PruneInvalidUnits();

	UMassEntitySubsystem* GetEntitySubsystem() const { return EntitySubsystem; }
	const TMap<FGameplayTag, TArray<FMassUnitEntityHandle>>& GetUnitTypeMap() const { return UnitTypeMap; }
	const TMap<int32, TArray<FMassUnitEntityHandle>>& GetTeamMap() const { return TeamMap; }

private:
	/** Rolls Visual.LookProfile from Visual.VariantSeed into the visual fragment (or restores the template mesh). */
	void ApplyLook(FMassUnitVisualFragment& Visual);

	/**
	 * Rolls Visual.AttitudeProfile from Visual.VariantSeed onto the template's values (speed, cooldown) and the attitude
	 * fields. Crowd is optional: a grouped, uncommanded unit's crowd base speed follows the new speed; a commanded
	 * unit's order keeps its speed and the command system's saved speed is updated instead.
	 */
	void ApplyAttitude(const FMassEntityHandle& Handle, FMassUnitStateFragment& State, FMassUnitVisualFragment& Visual, FMassUnitCrowdFragment* Crowd);

	/**
	 * Keeps a look or attitude profile and (look, non-dedicated) every asset it can roll referenced for the world's
	 * lifetime. Mass fragments are not scanned by the garbage collector, so the fragments' pointers are not enough.
	 */
	void RetainProfile(UObject* Profile);

	/** Profiles and their option assets in use by units (see RetainProfile). */
	UPROPERTY(Transient)
	TSet<TObjectPtr<UObject>> RetainedAssets;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	/** Shared body of single and batch creation. Options/VariantStream null = the single-unit defaults. */
	FMassUnitEntityHandle CreateUnitInternal(
		UUnitTemplate* Template,
		const FTransform& SpawnTransform,
		const FMassUnitBatchSpawnOptions* Options,
		FRandomStream* VariantStream);

	UUnitTemplate* GetOrCreateDefaultTemplate();

	/** Lazily created template used by CreateDefaultUnit. */
	UPROPERTY(Transient)
	TObjectPtr<UUnitTemplate> RuntimeDefaultTemplate = nullptr;

	FMassArchetypeHandle UnitArchetype;
	TArray<FMassUnitEntityHandle> AllUnits;
	TMap<FGameplayTag, TArray<FMassUnitEntityHandle>> UnitTypeMap;
	TMap<int32, TArray<FMassUnitEntityHandle>> TeamMap;

	/** Units with any status, so the status tick visits only those. */
	TSet<FMassEntityHandle> UnitsWithStatus;
	float NextStatusTickTime = 0.0f;

	/** One airborne unit. Sparse: only units in flight or settling have an entry. */
	struct FActiveLaunch
	{
		FVector Velocity = FVector::ZeroVector;
		/** Corpse tumble, degrees per second around the unit's local right (pitch) and forward (roll) axes. */
		float PitchRate = 0.0f;
		float RollRate = 0.0f;
		float LandZ = 0.0f;
		/** The landing trace hit ground: LandZ is real, CapLaunchLandingHeight leaves it alone. */
		bool bLandZFromTrace = false;
		float FlightTime = 0.0f;
		float SettleTime = 0.0f;
		FQuat SettleFrom = FQuat::Identity;
		float Yaw = 0.0f;
		int32 Bounces = 0;
		bool bDead = false;
		bool bSettling = false;
	};
	TMap<FMassEntityHandle, FActiveLaunch> ActiveLaunches;
	TArray<TPair<FMassUnitEntityHandle, FVector>> PendingRagdollRequests;

	/** Launched-status duration that holds a living unit down for any flight; landing swaps it for the knockdown. */
	static constexpr float LaunchFlightHoldSeconds = 30.0f;

	/** Landing-trace budget bookkeeping (UMassUnitSystemSettings::MaxLandingTracesPerFrame). */
	mutable uint64 LandingTraceFrame = 0;
	mutable int32 LandingTracesThisFrame = 0;

	float ResolveLandingZ(const FVector& Start, const FVector& Velocity, float Gravity, bool* bOutHitGround = nullptr) const;
	void StartCorpseTumble(FActiveLaunch& Launch, const FMassEntityHandle& Handle) const;

	void RemoveHandleFromIndexes(FMassUnitEntityHandle EntityHandle);

	/** Recomputes the slow multiplier and stun state after the status list changed. */
	void RefreshStatusEffects(FMassUnitAbilityFragment& Ability, FMassUnitStateFragment& State, FMassUnitVisualFragment* Visual) const;
	float GetStatusClockTime() const;
};
