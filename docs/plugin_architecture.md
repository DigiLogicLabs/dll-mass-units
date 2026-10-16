# UE5 Large-Scale Unit System Plugin Architecture

> Archived proposed architecture. The implemented native Mass 1.1 architecture is documented in `docs/architecture.md`.

## Overview

This document outlines the architecture for a high-performance large-scale unit system plugin for Unreal Engine 5.6. The plugin is designed to enable the rendering and simulation of thousands of units on screen simultaneously, with seamless integration with the Gameplay Ability System (GAS) and GASCompanion.

## Core Design Principles

1. **Performance-First Approach**: Optimize for handling thousands of units with minimal performance impact
2. **Layered Architecture**: Separate concerns between data management, visual representation, and gameplay logic
3. **Seamless Transitions**: Smooth transitions between different representation methods based on player interaction
4. **GAS Compatibility**: Full integration with Gameplay Ability System and GASCompanion
5. **Modularity**: Components that can be used independently or as a complete system
6. **Extensibility**: Easy to extend and customize for different game types and unit behaviors

## System Components

### 1. Mass Entity Management Layer

**Purpose**: Efficient data-oriented management of large numbers of entities

**Key Components**:
- **MassUnitEntityManager**: Central manager for all unit entities
- **MassUnitFragments**: Custom fragments for unit-specific data
  - `UnitStateFragment`: Stores unit state information
  - `UnitTargetFragment`: Stores targeting information
  - `UnitAbilityFragment`: Stores ability-related data
  - `UnitTeamFragment`: Stores team affiliation
- **MassUnitProcessors**: Custom processors for unit behavior
  - `UnitMovementProcessor`: Handles unit movement and navigation
  - `UnitFormationProcessor`: Manages unit formations and group behavior
  - `UnitCombatProcessor`: Handles combat interactions
  - `UnitVisibilityProcessor`: Manages LOD and culling
- **MassUnitTags**: Tags for identifying unit types and states
- **MassUnitSubsystem**: Game subsystem for managing unit-related operations

### 2. Visual Representation Layer

**Purpose**: Efficient rendering of thousands of units with appropriate level of detail

**Key Components**:
- **NiagaraUnitSystem**: Niagara system for rendering units
  - `UnitVertexAnimationModule`: Handles vertex animation for units
  - `UnitVisualStateModule`: Manages visual state transitions
  - `UnitEquipmentModule`: Handles equipment visualization
- **VertexAnimationManager**: Manages vertex animation textures and transitions
  - `AnimationTextureLibrary`: Library of pre-baked animation textures
  - `AnimationBlendSpace`: Handles blending between animations
- **UnitMeshPool**: Pool of skeletal meshes for player-interactive units
  - `MeshPoolManager`: Manages the pool of available meshes
  - `MeshTransitionHandler`: Handles transitions between vertex and skeletal representations

### 3. Gameplay Integration Layer

**Purpose**: Connect unit system with gameplay mechanics and GAS

**Key Components**:
- **GASUnitIntegration**: Bridge between Mass Entity System and GAS
  - `UnitAbilitySystemProxy`: Proxy for connecting units to GAS
  - `UnitAttributeSet`: Attribute set for units
  - `UnitAbilitySet`: Set of abilities for units
- **GASCompanionIntegration**: Specific integration with GASCompanion
  - `GSCUnitAdapter`: Adapter for GASCompanion compatibility
  - `GSCUnitBehaviorTree`: Behavior tree tasks for GASCompanion units
- **UnitGameplayEventSystem**: System for handling gameplay events for units
  - `EventDispatcher`: Dispatches events to appropriate handlers
  - `EventListeners`: Listens for and responds to events

### 4. Navigation and Pathfinding Layer

**Purpose**: Efficient pathfinding and navigation for large numbers of units

**Key Components**:
- **MassUnitNavigationSystem**: Custom navigation system for mass units
  - `NavigationGridManager`: Manages navigation grid data
  - `PathRequestBatcher`: Batches path requests for efficiency
  - `PathSmoother`: Smooths paths for more natural movement
- **FormationSystem**: System for managing unit formations
  - `FormationTemplates`: Pre-defined formation templates
  - `FormationSolver`: Solves formation positions for units
  - `FormationConstraints`: Handles constraints in formation positioning

### 5. Configuration and Customization Layer

**Purpose**: Allow for easy configuration and customization of the system

**Key Components**:
- **UnitTemplateLibrary**: Library of unit templates
  - `UnitTemplate`: Template for creating units
  - `UnitBehaviorProfile`: Profile for unit behavior
  - `UnitVisualProfile`: Profile for unit visuals
- **PluginSettings**: Settings for the plugin
  - `PerformanceSettings`: Settings for performance tuning
  - `VisualSettings`: Settings for visual quality
  - `BehaviorSettings`: Settings for unit behavior

## Data Flow and Interaction

1. **Unit Creation Flow**:
   - Unit templates are used to create unit entities in the Mass Entity System
   - Visual representation is created in Niagara
   - Gameplay data is initialized in the GAS integration layer

2. **Update Loop**:
   - Mass processors update unit state and behavior
   - Visual representation is updated based on unit state
   - GAS integration layer handles ability activation and effects

3. **Player Interaction Flow**:
   - When player interacts with units, they transition from vertex animation to skeletal mesh
   - GAS integration layer activates full ability system for interactive units
   - After interaction, units can transition back to vertex animation

4. **LOD System**:
   - Units close to player: Full skeletal animation and GAS integration
   - Units at medium distance: Vertex animation with simplified behavior
   - Units at far distance: Simplified vertex animation with minimal behavior
   - Units very far: Represented as particles or billboards

## Performance Optimization Strategies

1. **Batched Processing**: Process units in batches for efficient CPU utilization
2. **GPU-Based Animation**: Offload animation to GPU through vertex animation
3. **Spatial Partitioning**: Only process units that are relevant to gameplay
4. **LOD System**: Reduce detail for distant units
5. **Object Pooling**: Reuse objects rather than creating/destroying them
6. **Instanced Rendering**: Use instanced rendering for similar units
7. **Deferred Ability Processing**: Process abilities in batches
8. **Shared Resources**: Share resources between similar units

## Integration with Existing Systems

1. **GASCompanion Integration**:
   - Compatible with existing GASCompanion setup
   - Uses same ability and attribute definitions
   - Supports existing GASCompanion workflows

2. **Procedural Dungeon Integration**:
   - Units can navigate procedurally generated environments
   - Navigation system updates when environment changes
   - Formation system adapts to available space

3. **Roguelike Elements Integration**:
   - Support for unit progression and variation
   - Integration with loot and reward systems
   - Support for procedural unit generation

## Extension Points

1. **Custom Unit Types**: Create custom unit types with specific behaviors
2. **Custom Formations**: Define custom formation types
3. **Custom Abilities**: Create custom abilities for units
4. **Custom Visual Effects**: Add custom visual effects for units
5. **Custom AI Behaviors**: Define custom AI behaviors for units

## Technical Requirements

1. **Unreal Engine 5.6**
2. **C++ Implementation** for core systems
3. **Blueprint Exposure** for customization
4. **GASCompanion Plugin**
5. **Niagara Plugin**

This architecture provides a comprehensive foundation for a high-performance large-scale unit system that integrates with GASCompanion and supports the requirements specified by the user.
