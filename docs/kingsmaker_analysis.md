# Kingsmaker Game Analysis

> Archived third-party reference analysis. It is not a claim about this plugin's current behavior or performance.

## Overview
Kingsmaker is a game that combines third-person shooter gameplay with real-time strategy elements, featuring massive battles with thousands of units on screen simultaneously. The game serves as an excellent reference for our large-scale unit system plugin, as it implements many of the features our user requires.

## Technical Implementation

### Engine and Framework
- Built using Unreal Engine
- Designed specifically for next-gen hardware (not compatible with last-gen consoles)
- Does not use Lumen, screen-space global illumination, or Nanite to preserve performance

### Large-Scale Unit System

#### Rendering Approach
- Uses vertex animation computed on the GPU instead of traditional skeletal animation
- Every frame of every animation is written to a line of pixels in an offset texture
- Normal values stored in a separate texture
- Animation is processed through shaders rather than CPU calculations
- Supports up to 4,000 units on screen simultaneously (with potential for more)

#### Animation System
- Full-fledged blend space in vertex animation shader
- Supports complex transitions: eight-way walks blending into three-way runs blending into attacks
- Custom HLSL code for animation blending
- Different weapons and shields implemented as separate meshes with their own vertex animation shaders
- Seamless transition between vertex-animated units and skeletal-animated units

#### Niagara Integration
- Each soldier's helmet, shield, and weapon are individual Niagara particles
- Communication via data interfaces to track position and rotation
- Integer values used to define colors, allowing for multiple teams and thousands of variations
- Minimal texture memory usage for offset textures

#### Player Interaction System
- Units switch from vertex animation to skeletal animation when:
  - Player hovers crosshair over them
  - Units are within a few meters of the player's vehicle
- Uses a pool of approximately 20 skeletal meshes that are swapped in and out
- Seamless transition that's imperceptible to the player
- Allows for realistic reactions when interacting with units (shooting, driving through crowds)

#### AI and Pathfinding
- Heavily optimized, multi-threaded AI system
- Full 3D pathfinding through complex environments (castles with multiple floors, 100+ rooms)
- Custom pathfinding algorithms running on separate threads
- Custom collision solver for soldiers and cavalry
- Capable of handling 4,000 units using navmesh pathfinding simultaneously

## Relevance to Our Plugin

The Kingsmaker implementation provides several key insights for our plugin:

1. **Vertex Animation on GPU**: Using GPU-based vertex animation is crucial for achieving thousands of units with good performance.

2. **Niagara Integration**: Leveraging Niagara for equipment and effects provides flexibility and performance.

3. **Seamless Transitions**: The pool-based approach for transitioning between vertex and skeletal animation when units interact with the player is elegant and efficient.

4. **Multi-threaded AI**: Custom, optimized pathfinding is necessary for handling large numbers of units in complex 3D environments.

5. **Performance Considerations**: Forgoing certain high-end rendering features (Lumen, Nanite) may be necessary to maintain performance with thousands of units.

These techniques align perfectly with our research on Mass Entity System and Niagara with vertex animation, confirming that our approach is on the right track. The next step is to research how to integrate these systems with the Gameplay Ability System to provide the functionality our user requires.
