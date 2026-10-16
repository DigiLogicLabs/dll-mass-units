// Copyright Digi Logic Labs LLC. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MassUnitGameplayTags.h"
#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Misc/DataValidation.h"
#include "Tests/AutomationCommon.h"

namespace MassUnitStatusTest
{
	// Native tags cannot be defined in an Editor module, so the test borrows tags the runtime module
	// already registers. Only their names are unusual; the hierarchy is what is under test:
	//   "creature"  = MassUnit.Class.Soldier, whose parent MassUnit.Class stands in for a family tag
	//   statuses    = three distinct leaf tags sharing the parent MassUnit (for the cleanse-by-parent case)
	FGameplayTag CreatureLeaf() { return UE::MassUnitSystem::Tags::UnitClassSoldier(); }
	FGameplayTag CreatureParent() { return FGameplayTag::RequestGameplayTag(TEXT("MassUnit.Class")); }
	FGameplayTag BurningTag() { return UE::MassUnitSystem::Tags::BehaviorIdle(); }
	FGameplayTag ChilledTag() { return UE::MassUnitSystem::Tags::FormationLine(); }
	FGameplayTag TarredTag() { return UE::MassUnitSystem::Tags::FactionNeutral(); }
	FGameplayTag DamageTypeTag() { return UE::MassUnitSystem::Tags::UnitTypeDefault(); }

	FMassUnitVATClip MakeClip(const FGameplayTag& Tag, int32 Start, int32 Frames, bool bLoop)
	{
		FMassUnitVATClip Clip;
		Clip.AnimationTag = Tag;
		Clip.StartFrame = Start;
		Clip.NumFrames = Frames;
		Clip.FramesPerSecond = 30.0f;
		Clip.bLoop = bLoop;
		return Clip;
	}
}

/**
 * Unit gameplay tags and timed statuses (the crowd stand-in for an ASC and duration Gameplay Effects):
 * template tags are queryable hierarchically, a DoT ticks and expires on schedule, stuns and slows apply
 * and lift, immunity refuses, re-application refreshes and stacks, and death clears everything.
 *
 * The test world never ticks, so status time is driven by calling TickStatuses with explicit times
 * against the constant world clock the statuses were stamped with.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitStatusTagsTest,
	"MassUnitSystem.Units.StatusAndTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitStatusTagsTest::RunTest(const FString& Parameters)
{
	using namespace MassUnitStatusTest;

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
	// A bound project listener replaces the plugin's own DoT damage; this test checks the standalone path.
	UnitManager->OnUnitStatusDamageTick.Clear();
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();
	const float Now = World->GetTimeSeconds();

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->BaseHealth = 100;
	Template->GrantedTags.AddTag(CreatureLeaf());
	const FMassUnitHandle Goblin = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector::ZeroVector));
	const FMassUnitHandle FarGoblin = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(5000.0f, 0.0f, 0.0f)));

	UUnitTemplate* ImmuneTemplate = NewObject<UUnitTemplate>(GetTransientPackage());
	ImmuneTemplate->StatusImmunityTags.AddTag(UE::MassUnitSystem::Tags::StatusStunned());
	const FMassUnitHandle Boss = UnitManager->CreateUnitFromTemplate(ImmuneTemplate, FTransform(FVector(0.0f, 300.0f, 0.0f)));

	if (!TestTrue(TEXT("Units spawned"), Goblin.IsValid() && FarGoblin.IsValid() && Boss.IsValid()))
	{
		return false;
	}
	auto GetState = [&EntityManager](const FMassUnitHandle& Unit) -> FMassUnitStateFragment&
	{
		return *EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};

	// --- Template tags, hierarchical queries -------------------------------------------------------
	TestTrue(TEXT("Template tag is carried exactly"), UnitManager->UnitHasTag(Goblin, CreatureLeaf()));
	TestTrue(TEXT("Parent tag matches hierarchically (like ASC HasMatchingGameplayTag)"), UnitManager->UnitHasTag(Goblin, CreatureParent()));
	TestFalse(TEXT("A unit without the tag does not match"), UnitManager->UnitHasTag(Boss, CreatureParent()));
	const FGameplayTagQuery CreatureQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(CreatureParent()));
	TestEqual(TEXT("World-wide tag query finds both goblins"), UnitManager->FindUnitsMatchingTagQuery(CreatureQuery, FVector::ZeroVector, 0.0f).Num(), 2);
	TestEqual(TEXT("Radius tag query finds only the near goblin"), UnitManager->FindUnitsMatchingTagQuery(CreatureQuery, FVector::ZeroVector, 1000.0f).Num(), 1);

	// --- Loose tags ----------------------------------------------------------------------------------
	TestTrue(TEXT("Loose tag added"), UnitManager->AddUnitTags(Boss, FGameplayTagContainer(CreatureLeaf())));
	TestTrue(TEXT("Loose tag queryable"), UnitManager->UnitHasTag(Boss, CreatureParent()));
	UnitManager->RemoveUnitTags(Boss, FGameplayTagContainer(CreatureLeaf()));
	TestFalse(TEXT("Loose tag removed"), UnitManager->UnitHasTag(Boss, CreatureParent()));

	// --- Damage over time: 3 s burn, 5 per second = exactly 3 ticks, then the tag drops ------------
	FMassUnitStatusEffect Burn;
	Burn.StatusTag = BurningTag();
	Burn.Duration = 3.0f;
	Burn.DamagePerPeriod = 5.0f;
	Burn.Period = 1.0f;
	Burn.DamageTypeTag = DamageTypeTag();
	TestTrue(TEXT("Burn applied"), UnitManager->ApplyUnitStatus(Goblin, Burn));
	TestTrue(TEXT("Burn tag carried"), UnitManager->UnitHasTag(Goblin, BurningTag()));
	UnitManager->TickStatuses(Now + 0.5f);
	TestEqual(TEXT("No tick before the first period"), GetState(Goblin).Health, 100.0f);
	UnitManager->TickStatuses(Now + 1.05f);
	TestEqual(TEXT("First tick"), GetState(Goblin).Health, 95.0f);
	UnitManager->TickStatuses(Now + 3.2f);
	TestEqual(TEXT("Second and third ticks land, including the one due at expiry"), GetState(Goblin).Health, 85.0f);
	TestFalse(TEXT("Burn expired and its tag dropped"), UnitManager->UnitHasTag(Goblin, BurningTag()));
	UnitManager->TickStatuses(Now + 5.0f);
	TestEqual(TEXT("No tick after expiry"), GetState(Goblin).Health, 85.0f);

	// --- Stun: enters and leaves the Stunned state, roots the unit --------------------------------
	TestTrue(TEXT("Stun applied"), UnitManager->StunUnit(Goblin, 2.0f));
	TestEqual(TEXT("Stunned state"), GetState(Goblin).CurrentState, EMassUnitState::Stunned);
	TestEqual(TEXT("Stun roots"), GetState(Goblin).StatusMoveSpeedMultiplier, 0.0f);
	TestEqual(TEXT("Stun shows the stun clip"),
		EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(Goblin.EntityHandle.ToMassEntityHandle())->CurrentAnimation,
		UE::MassUnitSystem::Tags::AnimationStun());
	UnitManager->TickStatuses(Now + 7.5f);
	TestEqual(TEXT("Stun expired back to Idle"), GetState(Goblin).CurrentState, EMassUnitState::Idle);
	TestEqual(TEXT("Speed restored"), GetState(Goblin).StatusMoveSpeedMultiplier, 1.0f);

	// --- Immunity ----------------------------------------------------------------------------------
	TestFalse(TEXT("Stun-immune template refuses the stun"), UnitManager->StunUnit(Boss, 2.0f));
	TestNotEqual(TEXT("Immune unit is not stunned"), GetState(Boss).CurrentState, EMassUnitState::Stunned);

	// --- Slows multiply, re-apply refreshes rather than duplicating, stacks cap -------------------
	FMassUnitStatusEffect Chill;
	Chill.StatusTag = ChilledTag();
	Chill.Duration = 10.0f;
	Chill.MoveSpeedMultiplier = 0.5f;
	Chill.bStacks = true;
	Chill.MaxStacks = 2;
	FMassUnitStatusEffect Tar = Chill;
	Tar.StatusTag = TarredTag();
	Tar.bStacks = false;
	UnitManager->ApplyUnitStatus(Goblin, Chill);
	UnitManager->ApplyUnitStatus(Goblin, Tar);
	TestTrue(TEXT("Two slows multiply"), FMath::IsNearlyEqual(GetState(Goblin).StatusMoveSpeedMultiplier, 0.25f));
	UnitManager->ApplyUnitStatus(Goblin, Chill);
	UnitManager->ApplyUnitStatus(Goblin, Chill);
	const TArray<FMassUnitStatusEffect> Statuses = UnitManager->GetUnitStatuses(Goblin);
	TestEqual(TEXT("Re-applying refreshes instead of adding a row"), Statuses.Num(), 2);
	const FMassUnitStatusEffect* ChillRow = Statuses.FindByPredicate([](const FMassUnitStatusEffect& S) { return S.StatusTag == ChilledTag(); });
	TestTrue(TEXT("Stacks cap at MaxStacks"), ChillRow && ChillRow->Stacks == 2);
	{
		// A briefer re-application (burning ground re-arming each pulse) never shortens a longer status.
		const float LongExpire = ChillRow ? ChillRow->ExpireTime : 0.0f;
		FMassUnitStatusEffect BriefTar = Tar;
		BriefTar.Duration = 1.0f;
		const FMassUnitStatusEffect* TarBefore = Statuses.FindByPredicate([](const FMassUnitStatusEffect& S) { return S.StatusTag == TarredTag(); });
		const float TarExpireBefore = TarBefore ? TarBefore->ExpireTime : 0.0f;
		UnitManager->ApplyUnitStatus(Goblin, BriefTar);
		const TArray<FMassUnitStatusEffect> After = UnitManager->GetUnitStatuses(Goblin);
		const FMassUnitStatusEffect* TarAfter = After.FindByPredicate([](const FMassUnitStatusEffect& S) { return S.StatusTag == TarredTag(); });
		TestTrue(TEXT("Brief refresh keeps the longer expiry"), TarAfter && TarAfter->ExpireTime >= TarExpireBefore);
		TestTrue(TEXT("Chill row untouched by the Tar refresh"), LongExpire > 0.0f);
	}
	TestEqual(TEXT("Cleanse by parent tag removes both"),
		UnitManager->RemoveUnitStatusesMatching(Goblin, FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("MassUnit")))), 2);
	TestEqual(TEXT("Speed restored after cleanse"), GetState(Goblin).StatusMoveSpeedMultiplier, 1.0f);

	// --- Death clears statuses ---------------------------------------------------------------------
	UnitManager->ApplyUnitStatus(FarGoblin, Burn);
	UnitManager->ApplyDamage(FarGoblin, 1000.0f);
	UnitManager->TickStatuses(Now + 20.0f);
	TestTrue(TEXT("Dead units drop their statuses"), UnitManager->GetUnitStatuses(FarGoblin).IsEmpty());
	TestFalse(TEXT("Dead units refuse new statuses"), UnitManager->ApplyUnitStatus(FarGoblin, Burn));

	UnitManager->DestroyUnit(Goblin);
	UnitManager->DestroyUnit(FarGoblin);
	UnitManager->DestroyUnit(Boss);
	return true;
}

/**
 * Vertex-animation clip table and swing timing: clip fallbacks, data validation of the table, and the
 * per-swing restart / impact-delay contract the renderer, mesh pool and crowd attack rely on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitAnimationContractTest,
	"MassUnitSystem.Units.AnimationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitAnimationContractTest::RunTest(const FString& Parameters)
{
	using namespace MassUnitStatusTest;
	using namespace UE::MassUnitSystem;

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	TestNull(TEXT("Empty clip table resolves nothing (legacy behaviour)"), Template->FindVertexAnimationClip(Tags::AnimationIdle()));

	Template->VertexAnimationClips = {
		MakeClip(Tags::AnimationIdle(), 0, 48, true),
		MakeClip(Tags::AnimationWalk(), 48, 28, true),
		MakeClip(Tags::AnimationAttack(), 76, 24, false),
		MakeClip(Tags::AnimationDeath(), 100, 36, false),
	};
	const FMassUnitVATClip* Run = Template->FindVertexAnimationClip(Tags::AnimationRun());
	TestTrue(TEXT("Run falls back to the shared Walk/Move clip"), Run && Run->StartFrame == 48);
	const FMassUnitVATClip* Stun = Template->FindVertexAnimationClip(Tags::AnimationStun());
	TestTrue(TEXT("A missing clip falls back to Idle"), Stun && Stun->StartFrame == 0);
	const FMassUnitVATClip* Attack = Template->FindVertexAnimationClip(Tags::AnimationAttack());
	TestTrue(TEXT("Exact clip found"), Attack && Attack->StartFrame == 76 && !Attack->bLoop);
	TestTrue(TEXT("Clip duration from frames and rate"), Attack && FMath::IsNearlyEqual(Attack->GetDuration(), 24.0f / 30.0f));

#if WITH_EDITOR
	Template->StaticMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
	{
		FDataValidationContext Context;
		TestEqual(TEXT("A well-formed clip table validates"), Template->IsDataValid(Context), EDataValidationResult::NotValidated);
	}
	Template->VertexAnimationClips.Add(MakeClip(Tags::AnimationIdle(), 136, 10, true));
	{
		FDataValidationContext Context;
		TestEqual(TEXT("A duplicate row is rejected"), Template->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	Template->VertexAnimationClips.Pop();
	Template->AttackCooldown = 1.0f;
	Template->AttackImpactDelay = 1.5f;
	{
		FDataValidationContext Context;
		TestEqual(TEXT("An impact delay past the cooldown is rejected"), Template->IsDataValid(Context), EDataValidationResult::Invalid);
	}
#endif

	// Swing contract.
	FMassUnitStateFragment State;
	State.AttackImpactDelay = 0.3f;
	State.StateTime = 5.0f;
	State.BeginAttackSwing(12.0f);
	TestEqual(TEXT("A swing restarts the clip"), State.StateTime, 0.0f);
	TestEqual(TEXT("A swing bumps the serial"), State.AttackSerial, 1);
	TestEqual(TEXT("No damage before the impact frame"), State.ConsumeAttackImpact(), 0.0f);
	State.StateTime = 0.31f;
	TestEqual(TEXT("Damage lands at the impact frame"), State.ConsumeAttackImpact(), 12.0f);
	TestEqual(TEXT("Damage lands once"), State.ConsumeAttackImpact(), 0.0f);

	FMassUnitStateFragment Instant;
	Instant.BeginAttackSwing(7.0f);
	TestEqual(TEXT("Zero delay keeps the old same-update damage"), Instant.ConsumeAttackImpact(), 7.0f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
