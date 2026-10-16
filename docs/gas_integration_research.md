# GAS Integration for AI Units Research

> Archived research context, not current integration guidance. Version 1.1 uses Unreal's built-in GAS through an opt-in external-ASC bridge and has no GASCompanion dependency.

## Overview
The Gameplay Ability System (GAS) is a powerful framework in Unreal Engine for implementing gameplay abilities, attributes, and effects. GASCompanion is a plugin that extends GAS functionality and makes it more accessible. Integrating our large-scale unit system with GAS is crucial for compatibility with the user's existing project that uses GASCompanion.

## Key Concepts

### GASCompanion for AI

GASCompanion provides several components and classes specifically designed for AI integration:

1. **GSCBTTask_BlueprintBase**: A behavior tree task base class that can be extended to create custom AI tasks that interact with the Gameplay Ability System.

2. **AI Task Integration**: GASCompanion allows AI-controlled characters to use the same ability system as player characters, enabling consistent gameplay mechanics across all entities.

3. **Equipment Component**: Can be used to manage equipment and weapons for both player and AI characters, with abilities granted based on equipped items.

### Applying GAS to Mass Units

For our large-scale unit system, we need to consider how to efficiently integrate GAS with thousands of units:

1. **Selective Application**: Apply full GAS functionality only to units that require complex behaviors or are interacting with the player.

2. **Proxy Representation**: Use a proxy system where a single GAS component manages multiple units of the same type, reducing overhead.

3. **LOD-Based Ability Activation**: Similar to the visual LOD system, implement an ability LOD system where distant units use simplified ability logic.

4. **Batched Ability Processing**: Process abilities in batches for units of the same type to improve performance.

### Integration with Mass Entity System

The Mass Entity System can work alongside GAS through the following approaches:

1. **Fragment-Based Ability Data**: Store ability-related data in Mass Entity Fragments, allowing the Mass Entity System to manage the data efficiently.

2. **Processor-Based Ability Execution**: Create Mass Processors that execute abilities based on Fragment data, coordinating with the GAS framework.

3. **Event-Based Communication**: Use an event system to communicate between Mass Entity System and GAS, triggering ability activations when needed.

4. **Hybrid Approach**: Use Mass Entity System for basic units and transition to full GAS implementation when units interact with the player or require complex behaviors.

### GASCompanion-Specific Integration

For compatibility with the user's existing GASCompanion setup:

1. **GSCModularCharacter**: When units transition from vertex-animated to skeletal mesh representation, they can be instantiated as GSCModularCharacter or a derived class.

2. **Ability Granting**: Use the GrantedAbilities property of GSCAbilitySystemComponent to automatically grant appropriate abilities to units when they become active.

3. **Attribute Sets**: Apply appropriate attribute sets to units based on their type and role, using the GrantedAttributes property.

4. **Gameplay Effects**: Apply gameplay effects for ongoing behaviors like health regeneration or damage over time using the GrantedEffects property.

## Performance Considerations

1. **Pooling ASC Components**: Create a pool of AbilitySystemComponents that can be assigned to units as needed, rather than creating a new component for each unit.

2. **Simplified Ability Logic**: Use simplified versions of abilities for distant units, with full ability logic only applied to units near the player.

3. **Batched Attribute Updates**: Update attributes in batches rather than individually to reduce overhead.

4. **Deferred Ability Activation**: Queue ability activations and process them in batches during low-demand frames.

5. **Shared Gameplay Effect Contexts**: Use shared contexts for gameplay effects applied to multiple units of the same type.

## Implementation Strategy

For our large-scale unit system plugin, we recommend the following approach to GAS integration:

1. **Layered Integration**: 
   - Layer 1: Mass Entity System for basic unit representation and movement
   - Layer 2: Niagara and vertex animation for visual representation
   - Layer 3: GAS integration for units requiring complex behaviors or player interaction

2. **Transition System**:
   - Define clear conditions for when units should transition between layers
   - Implement seamless state transfer when transitioning from Mass Entity to GAS-controlled units

3. **Compatibility Layer**:
   - Create adapter classes that bridge between Mass Entity System and GASCompanion
   - Ensure all GASCompanion-specific features are properly supported

4. **Configuration Options**:
   - Provide plugin users with options to configure how deeply GAS is integrated with the unit system
   - Allow for customization of which unit types use which layers

## Relevance to Our Plugin

The GAS integration research provides crucial insights for our plugin:

1. **Compatibility**: Ensures our plugin works seamlessly with the user's existing GASCompanion setup.

2. **Performance**: Identifies strategies to maintain high performance while supporting GAS functionality for thousands of units.

3. **Flexibility**: Enables a flexible approach where different units can have different levels of GAS integration based on their needs.

4. **Consistency**: Allows for consistent gameplay mechanics between the user's existing characters and our large-scale unit system.

This research, combined with our findings on Mass Entity System, Niagara, and vertex animation techniques, provides a comprehensive foundation for designing our plugin architecture.
