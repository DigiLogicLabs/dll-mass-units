# Mass Entity System Research

> Archived research notes. Current implementation details are in `docs/architecture.md` and the plugin source.

## Overview
The Mass Entity System in Unreal Engine 5 is a data-oriented framework designed for high-performance simulation of large numbers of entities. This system is particularly relevant for our large-scale unit system plugin as it provides the foundation for efficiently managing thousands of units.

## Key Concepts

### Fragments
- Atomic pieces of data used in calculations (e.g., Transform, Velocity, LOD Index)
- Can be grouped into collections
- Examples relevant to our unit system: Position, Rotation, Health, State, Target

### Entities
- Collection instances associated with an ID
- Built by Fragment composition
- Compositions can be changed at runtime
- Data-only elements with no logic
- Similar to ECS (Entity Component System) architecture

### Archetypes
- Collections of Entities with identical composition
- Organized in memory "Chunks" for optimal performance
- Enables efficient batch processing of similar entities

### Processors
- Stateless classes that supply processing logic for Fragments
- Use EntityQuery to specify which types of Fragments they need
- Process batches of Fragments without regard to individual Entity identifiers
- Key to implementing behaviors like movement, targeting, and combat

### Tags
- Trivial Fragments containing no data
- Used as flags to indicate states or properties
- Part of an Entity's composition
- Useful for marking units as "selected", "attacking", "fleeing", etc.

### ChunkFragments
- Fragments associated with a Chunk instead of an Entity
- Used for per-Chunk data like LOD calculations
- Important for optimization of large unit groups

## Processing System

### MassCommandBuffer
- Used to request composition changes
- Commands are batch-processed at the end of processing
- Prevents issues with changing Entity composition during processing
- Essential for state transitions in unit behavior

### EntityView
- Provides safe access to Entities not currently being processed
- Optimized for performance
- Useful for interactions between different unit groups

## Subsystems

### MassEntity Manager
- Creates and hosts Entity Archetypes
- Interface for Entity operations (adding/removing Fragments)
- Responsible for moving Entities between Archetypes
- Core system for managing unit lifecycle

### Entity Templates
- Generated from MassEntityConfig assets
- Can declare sets of Traits to add to an Entity during creation
- Support inheritance from parent assets
- Will be useful for creating different unit types

### Traits
- Collective name for Fragments and Processors providing functionality
- Examples: Avoidance behavior, Look-At Target, State Tree
- Key to implementing reusable behaviors for units

## Relevance to Our Plugin

The Mass Entity System provides an excellent foundation for our large-scale unit system plugin:

1. **Performance Optimization**: The data-oriented design allows for efficient processing of thousands of units.
2. **Memory Management**: Archetype-based organization optimizes memory access patterns.
3. **Behavior Implementation**: Processors and Traits provide a framework for implementing complex unit behaviors.
4. **State Management**: Tags and composition changes enable robust state management.
5. **Integration Potential**: The system can be extended to work with GAS for more complex unit interactions.

Next steps include exploring how to integrate this system with Niagara for visual representation and investigating how to connect it with the Gameplay Ability System for advanced unit behaviors.
