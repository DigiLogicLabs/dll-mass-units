# Niagara and Vertex Animation Research

> Archived research notes. Current Niagara parameters and the asset-free representation fallback are documented in `docs/usage.md`.

## Overview
Niagara is Unreal Engine's advanced visual effects system that can be leveraged for large-scale unit simulation. When combined with vertex animation techniques, it provides a powerful solution for rendering thousands of units on screen with minimal performance impact.

## Key Concepts

### Niagara for Crowd Simulation
- GPU-based particle system capable of simulating thousands of entities
- Can replace traditional AI actors for distant or numerous units
- Supports complex behaviors through custom modules
- Integrates with vertex animation techniques for efficient rendering
- Significantly reduces CPU overhead compared to traditional actor-based approaches

### Vertex Animation Techniques
- **Vertex Animation Textures (VAT)**: Store animation data in textures rather than skeletal animations
- **AnimToTexture Plugin**: Official UE5 tool for baking skeletal animations into vertex animations
- Animations are rendered to textures containing:
  - Position maps
  - Normal maps
  - Pivot information
- Dramatically reduces memory and processing requirements
- Enables thousands of animated units with minimal performance impact

### Animation Player Module
- Niagara module for playing back vertex animations
- Supports animation blending and transitions
- Can be controlled through parameters for varied behaviors
- Enables units to display appropriate animations based on state

### Finish Current Animation First Module
- Ensures animations complete before transitioning
- Prevents visual artifacts from abrupt animation changes
- Important for maintaining visual quality in crowd simulations

## Implementation Approaches

### Smart Mesh Particles
- Niagara particles that can replace basic AI units
- Capable of following paths, avoiding obstacles, and responding to environment
- Can be configured with different behaviors and animations
- Transition to full skeletal meshes when player interacts with them

### LOD System for Crowds
- Different representation techniques based on distance from camera:
  - Close units: Full skeletal meshes with detailed animations
  - Medium distance: Simplified skeletal meshes or high-quality vertex animations
  - Far distance: Basic vertex animations or simplified representations
  - Very far: Billboards or simplified particles

### AnimToTexture Workflow
1. Create skeletal animations in traditional animation tools
2. Use AnimToTexture plugin to bake animations to textures
3. Create material setup to interpret animation textures
4. Set up Niagara system to use these materials
5. Configure behavior modules for movement and interaction

## Performance Considerations

### Optimizations
- Instanced rendering for similar units
- Culling techniques to avoid rendering off-screen units
- Batched processing of unit behaviors
- Simplified physics for distant units
- Reduced animation complexity based on distance

### Scalability Settings
- Configurable density based on hardware capabilities
- Adjustable animation quality and variety
- Dynamic LOD transitions
- Performance-driven behavior complexity

## Integration with Other Systems

### Mass Entity System Integration
- Mass Entity can handle the logical representation and behavior
- Niagara can handle the visual representation
- Synchronization between systems through shared data

### Navigation and Pathing
- Can use simplified navigation meshes for large groups
- Group-based pathfinding to reduce calculation overhead
- Swarm behaviors implemented through Niagara forces

### Gameplay Ability System Connection
- GAS can be applied to units that require complex behaviors
- Transition between Niagara representation and GAS-controlled actors when needed
- Shared attribute system between visual and logical representations

## Relevance to Our Plugin

The combination of Niagara and vertex animation techniques provides the foundation for our large-scale unit system:

1. **Performance**: GPU-accelerated simulation allows for thousands of units
2. **Visual Quality**: Vertex animations maintain visual fidelity with minimal overhead
3. **Behavior Complexity**: Can implement varied behaviors through custom modules
4. **Seamless Transitions**: Units can transition between simplified and detailed representations
5. **Integration Potential**: Can work alongside Mass Entity System and GAS for a complete solution

Next steps include exploring how to integrate these techniques with the Gameplay Ability System and designing the overall plugin architecture.
