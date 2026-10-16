# UE5 Large-Scale Unit System Plugin Technical Specification

> Archived proposal containing class names and dependencies that were never part of the current API. Use `docs/api.md` and the public headers as the implementation contract.

## 1. Introduction

### 1.1 Purpose
This technical specification details the implementation of a high-performance large-scale unit system plugin for Unreal Engine 5.6. The plugin enables the rendering and simulation of thousands of units on screen simultaneously, with seamless integration with the Gameplay Ability System (GAS) and GASCompanion.

### 1.2 Scope
The plugin provides a complete solution for:
- Managing thousands of units with minimal performance impact
- Rendering units using Niagara and vertex animation techniques
- Integrating with GASCompanion for ability and attribute management
- Providing pathfinding and formation systems for unit movement
- Supporting seamless transitions between different representation methods

### 1.3 Target Users
- Developers using UE5 with GASCompanion
- Projects requiring large-scale unit simulation (RTS, RPGs, strategy games)
- Games with procedurally generated environments and roguelike elements

## 2. System Architecture

### 2.1 High-Level Architecture
The plugin follows a layered architecture with five primary layers:
1. Mass Entity Management Layer
2. Visual Representation Layer
3. Gameplay Integration Layer
4. Navigation and Pathfinding Layer
5. Configuration and Customization Layer

### 2.2 Dependencies
- Unreal Engine 5.6
- GASCompanion Plugin
- Niagara Plugin

### 2.3 Plugin Structure
```
MassUnitSystem/
├── Source/
│   ├── MassUnitSystemRuntime/
│   │   ├── Public/
│   │   │   ├── Core/
│   │   │   ├── Entity/
│   │   │   ├── Visual/
│   │   │   ├── Gameplay/
│   │   │   ├── Navigation/
│   │   │   └── Config/
│   │   └── Private/
│   │       ├── Core/
│   │       ├── Entity/
│   │       ├── Visual/
│   │       ├── Gameplay/
│   │       ├── Navigation/
│   │       └── Config/
│   └── MassUnitSystemEditor/
│       ├── Public/
│       └── Private/
├── Content/
│   ├── Blueprints/
│   ├── Materials/
│   ├── Textures/
│   ├── Meshes/
│   └── Niagara/
└── Documentation/
```

## 3. Detailed Component Specifications

### 3.1 Mass Entity Management Layer

#### 3.1.1 MassUnitEntityManager
- **Class**: `UMassUnitEntityManager`
- **Responsibilities**:
  - Create and destroy unit entities
  - Manage entity lifecycle
  - Coordinate between different systems
- **Key Methods**:
  - `CreateUnitFromTemplate(UUnitTemplate* Template, FTransform SpawnTransform)`
  - `DestroyUnit(FMassEntityHandle EntityHandle)`
  - `GetUnitsByType(FGameplayTag UnitType)`
  - `GetUnitsByTeam(int32 TeamID)`

#### 3.1.2 MassUnitFragments
- **UnitStateFragment**
  - **Class**: `FMassUnitStateFragment`
  - **Data**:
    - `EUnitState CurrentState`
    - `float StateTime`
    - `FGameplayTag UnitType`
    - `int32 UnitLevel`

- **UnitTargetFragment**
  - **Class**: `FMassUnitTargetFragment`
  - **Data**:
    - `FMassEntityHandle TargetEntity`
    - `FVector TargetLocation`
    - `float TargetPriority`

- **UnitAbilityFragment**
  - **Class**: `FMassUnitAbilityFragment`
  - **Data**:
    - `TArray<FGameplayAbilitySpecHandle> AbilityHandles`
    - `TArray<FGameplayTag> ActiveEffectTags`
    - `TMap<FGameplayAttribute, float> AttributeValues`

- **UnitTeamFragment**
  - **Class**: `FMassUnitTeamFragment`
  - **Data**:
    - `int32 TeamID`
    - `FLinearColor TeamColor`
    - `FGameplayTag TeamFaction`

#### 3.1.3 MassUnitProcessors
- **UnitMovementProcessor**
  - **Class**: `UMassUnitMovementProcessor`
  - **Responsibilities**:
    - Update unit positions based on movement data
    - Handle collision avoidance
    - Apply movement constraints
  - **Key Methods**:
    - `Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)`
    - `CalculateMovementVector(FMassEntityHandle Entity, float DeltaTime)`

- **UnitFormationProcessor**
  - **Class**: `UMassUnitFormationProcessor`
  - **Responsibilities**:
    - Maintain formation integrity
    - Calculate formation positions
    - Handle formation transitions
  - **Key Methods**:
    - `Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)`
    - `CalculateFormationPosition(FMassEntityHandle Entity, FFormationData FormationData)`

- **UnitCombatProcessor**
  - **Class**: `UMassUnitCombatProcessor`
  - **Responsibilities**:
    - Process combat interactions
    - Apply damage and effects
    - Trigger ability activations
  - **Key Methods**:
    - `Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)`
    - `ProcessCombatInteraction(FMassEntityHandle Attacker, FMassEntityHandle Target)`

- **UnitVisibilityProcessor**
  - **Class**: `UMassUnitVisibilityProcessor`
  - **Responsibilities**:
    - Determine unit visibility
    - Manage LOD transitions
    - Handle culling
  - **Key Methods**:
    - `Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)`
    - `DetermineVisibilityState(FMassEntityHandle Entity, FVector ViewLocation)`

#### 3.1.4 MassUnitSubsystem
- **Class**: `UMassUnitSubsystem`
- **Responsibilities**:
  - Provide global access to unit system
  - Manage system-wide settings
  - Coordinate between subsystems
- **Key Methods**:
  - `Initialize(FSubsystemCollectionBase& Collection)`
  - `Deinitialize()`
  - `Tick(float DeltaTime)`
  - `GetUnitManager()`

### 3.2 Visual Representation Layer

#### 3.2.1 NiagaraUnitSystem
- **Class**: `UNiagaraUnitSystem`
- **Responsibilities**:
  - Render units using Niagara
  - Manage visual state
  - Handle LOD transitions
- **Key Components**:
  - `UnitVertexAnimationModule`
  - `UnitVisualStateModule`
  - `UnitEquipmentModule`
- **Key Methods**:
  - `Initialize()`
  - `UpdateUnitVisuals(const TArray<FMassEntityHandle>& Entities)`
  - `SetLODLevel(int32 LODLevel)`

#### 3.2.2 VertexAnimationManager
- **Class**: `UVertexAnimationManager`
- **Responsibilities**:
  - Manage vertex animation textures
  - Handle animation blending
  - Transition between animations
- **Key Components**:
  - `AnimationTextureLibrary`
  - `AnimationBlendSpace`
- **Key Methods**:
  - `Initialize()`
  - `GetAnimationTexture(FGameplayTag AnimationTag)`
  - `BlendAnimations(FGameplayTag FromAnim, FGameplayTag ToAnim, float BlendAlpha)`

#### 3.2.3 UnitMeshPool
- **Class**: `UUnitMeshPool`
- **Responsibilities**:
  - Manage pool of skeletal meshes
  - Handle transitions between representations
  - Optimize mesh usage
- **Key Components**:
  - `MeshPoolManager`
  - `MeshTransitionHandler`
- **Key Methods**:
  - `Initialize(int32 PoolSize)`
  - `GetMeshForUnit(FMassEntityHandle Entity)`
  - `ReleaseMesh(USkeletalMeshComponent* Mesh)`
  - `TransitionToSkeletal(FMassEntityHandle Entity)`
  - `TransitionToVertex(FMassEntityHandle Entity)`

### 3.3 Gameplay Integration Layer

#### 3.3.1 GASUnitIntegration
- **Class**: `UGASUnitIntegration`
- **Responsibilities**:
  - Bridge between Mass Entity System and GAS
  - Manage ability activation
  - Handle attribute updates
- **Key Components**:
  - `UnitAbilitySystemProxy`
  - `UnitAttributeSet`
  - `UnitAbilitySet`
- **Key Methods**:
  - `Initialize()`
  - `ActivateAbility(FMassEntityHandle Entity, FGameplayTag AbilityTag)`
  - `UpdateAttributes(FMassEntityHandle Entity, const TMap<FGameplayAttribute, float>& Attributes)`

#### 3.3.2 GASCompanionIntegration
- **Class**: `UGASCompanionIntegration`
- **Responsibilities**:
  - Provide compatibility with GASCompanion
  - Adapt unit system to GASCompanion workflows
  - Support GASCompanion features
- **Key Components**:
  - `GSCUnitAdapter`
  - `GSCUnitBehaviorTree`
- **Key Methods**:
  - `Initialize()`
  - `CreateGSCCompatibleUnit(FMassEntityHandle Entity)`
  - `ApplyGSCAbilitySet(FMassEntityHandle Entity, UGSCAbilitySet* AbilitySet)`

#### 3.3.3 UnitGameplayEventSystem
- **Class**: `UUnitGameplayEventSystem`
- **Responsibilities**:
  - Dispatch gameplay events
  - Manage event listeners
  - Coordinate event responses
- **Key Components**:
  - `EventDispatcher`
  - `EventListeners`
- **Key Methods**:
  - `Initialize()`
  - `DispatchEvent(FGameplayTag EventTag, FMassEntityHandle Entity, const FGameplayEventData& EventData)`
  - `RegisterListener(FGameplayTag EventTag, const FOnGameplayEvent& Listener)`

### 3.4 Navigation and Pathfinding Layer

#### 3.4.1 MassUnitNavigationSystem
- **Class**: `UMassUnitNavigationSystem`
- **Responsibilities**:
  - Provide efficient pathfinding for units
  - Manage navigation data
  - Handle path requests
- **Key Components**:
  - `NavigationGridManager`
  - `PathRequestBatcher`
  - `PathSmoother`
- **Key Methods**:
  - `Initialize()`
  - `RequestPath(FMassEntityHandle Entity, FVector Destination)`
  - `UpdateNavigationData(UWorld* World)`
  - `ProcessPathRequests()`

#### 3.4.2 FormationSystem
- **Class**: `UFormationSystem`
- **Responsibilities**:
  - Manage unit formations
  - Calculate formation positions
  - Handle formation transitions
- **Key Components**:
  - `FormationTemplates`
  - `FormationSolver`
  - `FormationConstraints`
- **Key Methods**:
  - `Initialize()`
  - `CreateFormation(FGameplayTag FormationType, const TArray<FMassEntityHandle>& Units)`
  - `UpdateFormation(FFormationHandle FormationHandle, FVector Destination, FRotator Orientation)`
  - `CalculateFormationPositions(FFormationHandle FormationHandle)`

### 3.5 Configuration and Customization Layer

#### 3.5.1 UnitTemplateLibrary
- **Class**: `UUnitTemplateLibrary`
- **Responsibilities**:
  - Provide templates for unit creation
  - Manage unit profiles
  - Support customization
- **Key Components**:
  - `UnitTemplate`
  - `UnitBehaviorProfile`
  - `UnitVisualProfile`
- **Key Methods**:
  - `Initialize()`
  - `GetUnitTemplate(FGameplayTag UnitType)`
  - `CreateUnitTemplate(FGameplayTag UnitType, const FUnitTemplateData& TemplateData)`

#### 3.5.2 PluginSettings
- **Class**: `UMassUnitSystemSettings`
- **Responsibilities**:
  - Store plugin settings
  - Provide configuration interface
  - Manage default values
- **Key Components**:
  - `PerformanceSettings`
  - `VisualSettings`
  - `BehaviorSettings`
- **Key Methods**:
  - `GetSettings()`
  - `SaveSettings()`
  - `ResetToDefaults()`

## 4. Data Structures and Interfaces

### 4.1 Core Data Structures

#### 4.1.1 UnitTemplateData
```cpp
struct FUnitTemplateData
{
    FGameplayTag UnitType;
    FGameplayTag UnitClass;
    int32 BaseHealth;
    int32 BaseDamage;
    float MoveSpeed;
    TArray<FGameplayTag> DefaultAbilities;
    TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
    TSoftObjectPtr<UStaticMesh> StaticMesh;
    TSoftObjectPtr<UTexture2D> VertexAnimationTexture;
    TSoftObjectPtr<UTexture2D> NormalMapTexture;
    TArray<FGameplayTag> AnimationTags;
    FGameplayTag DefaultBehavior;
    FGameplayTag DefaultFormation;
    int32 TeamID;
};
```

#### 4.1.2 FormationData
```cpp
struct FFormationData
{
    FGameplayTag FormationType;
    FVector FormationCenter;
    FRotator FormationOrientation;
    float UnitSpacing;
    int32 RowCount;
    int32 ColumnCount;
    TArray<FVector> FormationOffsets;
    TArray<FMassEntityHandle> AssignedUnits;
    FFormationHandle FormationHandle;
};
```

#### 4.1.3 UnitVisualState
```cpp
struct FUnitVisualState
{
    FGameplayTag CurrentAnimation;
    FGameplayTag TargetAnimation;
    float BlendAlpha;
    FVector Position;
    FRotator Rotation;
    FVector Scale;
    int32 LODLevel;
    bool bIsVisible;
    bool bUseSkeletalMesh;
    TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;
};
```

### 4.2 Key Interfaces

#### 4.2.1 IMassUnitInterface
```cpp
class IMassUnitInterface
{
public:
    virtual void Initialize(FMassEntityHandle EntityHandle) = 0;
    virtual void Deinitialize() = 0;
    virtual void UpdateVisuals(float DeltaTime) = 0;
    virtual void SetState(EUnitState NewState) = 0;
    virtual void ActivateAbility(FGameplayTag AbilityTag) = 0;
    virtual void ApplyEffect(FGameplayTag EffectTag, AActor* Instigator) = 0;
    virtual void SetTarget(FMassEntityHandle TargetEntity, FVector TargetLocation) = 0;
    virtual void JoinFormation(FFormationHandle FormationHandle, int32 FormationSlot) = 0;
    virtual void LeaveFormation() = 0;
};
```

#### 4.2.2 IUnitVisualProvider
```cpp
class IUnitVisualProvider
{
public:
    virtual void Initialize() = 0;
    virtual void Deinitialize() = 0;
    virtual void UpdateVisuals(const TArray<FMassEntityHandle>& Entities, float DeltaTime) = 0;
    virtual void SetLODLevel(int32 LODLevel) = 0;
    virtual void TransitionToSkeletal(FMassEntityHandle Entity) = 0;
    virtual void TransitionToVertex(FMassEntityHandle Entity) = 0;
    virtual USkeletalMeshComponent* GetSkeletalMeshForEntity(FMassEntityHandle Entity) = 0;
};
```

#### 4.2.3 IGASUnitProvider
```cpp
class IGASUnitProvider
{
public:
    virtual void Initialize() = 0;
    virtual void Deinitialize() = 0;
    virtual UAbilitySystemComponent* GetAbilitySystemForEntity(FMassEntityHandle Entity) = 0;
    virtual void GrantAbility(FMassEntityHandle Entity, TSubclassOf<UGameplayAbility> AbilityClass) = 0;
    virtual void ActivateAbility(FMassEntityHandle Entity, FGameplayTag AbilityTag) = 0;
    virtual void ApplyGameplayEffect(FMassEntityHandle Entity, TSubclassOf<UGameplayEffect> EffectClass, AActor* Instigator) = 0;
    virtual float GetAttributeValue(FMassEntityHandle Entity, FGameplayAttribute Attribute) = 0;
    virtual void SetAttributeValue(FMassEntityHandle Entity, FGameplayAttribute Attribute, float Value) = 0;
};
```

## 5. Performance Considerations

### 5.1 Optimization Techniques
- **Batched Processing**: Process units in batches for efficient CPU utilization
- **GPU-Based Animation**: Offload animation to GPU through vertex animation
- **Spatial Partitioning**: Only process units that are relevant to gameplay
- **LOD System**: Reduce detail for distant units
- **Object Pooling**: Reuse objects rather than creating/destroying them
- **Instanced Rendering**: Use instanced rendering for similar units
- **Deferred Ability Processing**: Process abilities in batches
- **Shared Resources**: Share resources between similar units

### 5.2 Performance Targets
- **Minimum**: 5,000 units at 30 FPS on mid-range hardware
- **Target**: 10,000 units at 60 FPS on high-end hardware
- **Maximum**: 20,000+ units with reduced visual fidelity

### 5.3 Memory Management
- **Pooling**: Use object pools for frequently created/destroyed objects
- **Shared Resources**: Share textures, materials, and meshes between units
- **Streaming**: Stream in resources as needed based on visibility
- **Garbage Collection**: Minimize garbage collection by reusing objects

## 6. Integration Guidelines

### 6.1 GASCompanion Integration
- Use `GSCUnitAdapter` to bridge between Mass Entity System and GASCompanion
- Implement `IGASUnitProvider` interface for GASCompanion compatibility
- Use GASCompanion's ability and attribute definitions
- Support GASCompanion's workflow for ability activation and effect application

### 6.2 Procedural Dungeon Integration
- Update navigation data when environment changes
- Implement dynamic obstacle avoidance
- Support runtime path recalculation
- Provide formation adaptation for confined spaces

### 6.3 Roguelike Elements Integration
- Support unit progression and variation
- Implement loot and reward systems
- Enable procedural unit generation
- Support unit ability and attribute randomization

## 7. Extension Points

### 7.1 Custom Unit Types
- Create custom unit templates
- Implement custom unit behaviors
- Define custom unit visuals
- Specify custom unit abilities and attributes

### 7.2 Custom Formations
- Create custom formation templates
- Implement custom formation solvers
- Define custom formation constraints
- Specify custom formation behaviors

### 7.3 Custom Abilities
- Create custom ability blueprints
- Implement custom ability tasks
- Define custom ability effects
- Specify custom ability animations

## 8. Testing and Validation

### 8.1 Performance Testing
- Benchmark with varying numbers of units
- Test on different hardware configurations
- Measure CPU, GPU, and memory usage
- Identify performance bottlenecks

### 8.2 Functional Testing
- Verify unit behavior and movement
- Test formation integrity
- Validate ability activation and effects
- Ensure seamless transitions between representations

### 8.3 Integration Testing
- Test with GASCompanion
- Verify compatibility with procedural dungeons
- Validate roguelike element integration
- Ensure plugin works with existing projects

## 9. Deployment and Distribution

### 9.1 Plugin Packaging
- Package plugin with all required assets
- Include documentation and examples
- Provide sample projects
- Include performance benchmarks

### 9.2 Installation Instructions
- Add plugin to project
- Configure plugin settings
- Set up unit templates
- Integrate with existing systems

### 9.3 Version Control
- Use semantic versioning
- Maintain backward compatibility
- Document breaking changes
- Provide migration guides

## 10. Future Enhancements

### 10.1 Planned Features
- Advanced AI behaviors
- Network replication support
- Enhanced visual effects
- Additional formation types
- Performance optimizations

### 10.2 Research Areas
- Machine learning for unit behavior
- Advanced crowd simulation techniques
- GPU-based pathfinding
- Procedural animation blending
- Dynamic LOD system improvements

This technical specification provides a comprehensive guide for implementing the UE5 Large-Scale Unit System Plugin, ensuring it meets the requirements for high performance, GASCompanion integration, and support for procedural environments and roguelike elements.
