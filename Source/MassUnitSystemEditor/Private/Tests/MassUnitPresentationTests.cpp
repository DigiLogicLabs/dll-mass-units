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
#include "Tests/AutomationCommon.h"

/**
 * Presentation replication API (docs/crowd_replication_plan.md P1): what a client puppet is shown from a record.
 * - every unit gets a stored variant seed at spawn (the value the renderer always derived), and it can be overridden;
 * - GetUnitPresentation reports state + a 5-bit swing counter that moves with every attack swing;
 * - ApplyPresentationState shows Attacking (restarting the clip per new swing, NEVER queuing damage), Stunned, and
 *   hands locomotion back on Idle/Moving; Dead is ignored and a corpse is never revived.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitPresentationTest,
	"MassUnitSystem.Units.PresentationReplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitPresentationTest::RunTest(const FString& Parameters)
{
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
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();
	auto GetState = [&EntityManager](const FMassUnitHandle& Unit) -> FMassUnitStateFragment&
	{
		return *EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};
	auto GetVisual = [&EntityManager](const FMassUnitHandle& Unit) -> FMassUnitVisualFragment&
	{
		return *EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};
	namespace Tags = UE::MassUnitSystem::Tags;

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	const FMassUnitHandle Server = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(0.0f, 0.0f, 100.0f)));
	const FMassUnitHandle Puppet = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(500.0f, 0.0f, 100.0f)));
	if (!TestTrue(TEXT("Units spawned"), Server.IsValid() && Puppet.IsValid()))
	{
		return false;
	}

	// --- Variant seed --------------------------------------------------------------------------------
	const float ServerSeed = UnitManager->GetUnitVariantSeed(Server);
	TestTrue(TEXT("A spawned unit has a stored 0-1 variant seed"), ServerSeed >= 0.0f && ServerSeed <= 1.0f);
	TestEqual(TEXT("The stored seed is the index value the renderer always used (single player unchanged)"),
		ServerSeed, FMath::Frac(static_cast<float>(Server.EntityHandle.ToMassEntityHandle().Index) * 0.6180339887f), 1e-5f);
	TestTrue(TEXT("The seed can be overridden (a client takes the server's value)"), UnitManager->SetUnitVariantSeed(Puppet, ServerSeed));
	TestEqual(TEXT("so both machines now hold the same seed"), UnitManager->GetUnitVariantSeed(Puppet), ServerSeed, 1e-5f);

	// --- Server side: state + swing counter ----------------------------------------------------------
	FMassUnitStateFragment& ServerState = GetState(Server);
	ServerState.CurrentState = EMassUnitState::Attacking;
	ServerState.BeginAttackSwing(10.0f);
	EMassUnitState Reported = EMassUnitState::Idle;
	uint8 Counter = 0;
	TestTrue(TEXT("GetUnitPresentation succeeds"), UnitManager->GetUnitPresentation(Server, Reported, Counter));
	TestEqual(TEXT("it reports Attacking"), static_cast<int32>(Reported), static_cast<int32>(EMassUnitState::Attacking));
	TestEqual(TEXT("and the 5-bit swing counter"), static_cast<int32>(Counter), ServerState.AttackSerial & 0x1F);

	// --- Client side: attack swings restart the clip, never queue damage ------------------------------
	FMassUnitStateFragment& PuppetState = GetState(Puppet);
	PuppetState.StateTime = 0.7f;
	const int32 SerialBefore = PuppetState.AttackSerial;
	TestTrue(TEXT("ApplyPresentationState(Attacking) succeeds"), UnitManager->ApplyPresentationState(Puppet, EMassUnitState::Attacking, 3));
	TestEqual(TEXT("the puppet is Attacking"), static_cast<int32>(PuppetState.CurrentState), static_cast<int32>(EMassUnitState::Attacking));
	TestTrue(TEXT("with the attack clip"), GetVisual(Puppet).CurrentAnimation == Tags::AnimationAttack());
	TestEqual(TEXT("restarted (state time 0)"), PuppetState.StateTime, 0.0f);
	TestEqual(TEXT("and its local serial moved so a pooled skeletal mesh restarts too"), PuppetState.AttackSerial, SerialBefore + 1);
	TestEqual(TEXT("NO damage is queued on a puppet"), PuppetState.PendingAttackDamage, 0.0f);
	PuppetState.StateTime = 0.4f;
	UnitManager->ApplyPresentationState(Puppet, EMassUnitState::Attacking, 3);
	TestEqual(TEXT("The same swing again does not restart the clip"), PuppetState.StateTime, 0.4f);
	UnitManager->ApplyPresentationState(Puppet, EMassUnitState::Attacking, 4);
	TestEqual(TEXT("A new swing counter restarts it"), PuppetState.StateTime, 0.0f);

	// --- Stun, then back to locomotion ----------------------------------------------------------------
	UnitManager->ApplyPresentationState(Puppet, EMassUnitState::Stunned, 4);
	TestEqual(TEXT("Stunned shows"), static_cast<int32>(PuppetState.CurrentState), static_cast<int32>(EMassUnitState::Stunned));
	TestTrue(TEXT("with the stun clip"), GetVisual(Puppet).CurrentAnimation == Tags::AnimationStun());
	UnitManager->ApplyPresentationState(Puppet, EMassUnitState::Moving, 4);
	TestEqual(TEXT("Locomotion hands back to the movement processor (Idle until the puppet moves)"),
		static_cast<int32>(PuppetState.CurrentState), static_cast<int32>(EMassUnitState::Idle));
	TestTrue(TEXT("with the idle clip"), GetVisual(Puppet).CurrentAnimation == Tags::AnimationIdle());

	// --- Death is never driven by presentation ---------------------------------------------------------
	TestFalse(TEXT("Dead is ignored (health replication kills puppets)"), UnitManager->ApplyPresentationState(Puppet, EMassUnitState::Dead, 4));
	TestNotEqual(TEXT("so the puppet is not dead"), static_cast<int32>(PuppetState.CurrentState), static_cast<int32>(EMassUnitState::Dead));
	PuppetState.CurrentState = EMassUnitState::Dead;
	TestFalse(TEXT("and a corpse is never revived by a presentation record"), UnitManager->ApplyPresentationState(Puppet, EMassUnitState::Attacking, 5));
	TestEqual(TEXT("it stays dead"), static_cast<int32>(PuppetState.CurrentState), static_cast<int32>(EMassUnitState::Dead));

	UnitManager->DestroyUnit(Server);
	UnitManager->DestroyUnit(Puppet);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
