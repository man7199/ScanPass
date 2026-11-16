# Procedural Grass (UE 5.8 plugin)

GPU driven grass for a football pitch, following *Procedural Grass in Ghost of Tsushima*
(Eric Wohllaib, GDC 2021). Everything is custom: no foliage system, no Niagara, no grass mesh asset.

## How a frame works

1. **CPU, per view** (`GatherVisibleTiles`): the field is split into tiles. Each tile is frustum
   culled and given a LOD from its closest distance to the camera.
2. **Compute, one dispatch per LOD** (`GenerateBladesCS`): one thread per blade. It builds the blade
   (jittered position, Voronoi clumping, mowing stripe lean, wind from scrolling noise, push from
   interactors), fades it by a continuous per blade LOD, frustum culls it and appends it to the
   instance buffer. Instance counts go straight into the indirect draw arguments.
3. **Two indirect draws** (`FGrassVertexFactory`): near blades use 15 vertices, far blades 7. The
   vertex shader reads the blade and builds it from a cubic Bezier curve, with rounded normals and
   view space thickening. There are no vertex buffers.

The compute passes are added from the engine's pre render delegate, after visibility has gathered
the mesh batches and before anything draws (`FGrassRendererExtension`), the same structure as the
engine's Virtual Heightfield Mesh plugin.

## What is taken from the talk

| Talk | Here |
| --- | --- |
| Per tile compute generation into an instance buffer, indirect draw | `GrassGenerate.usf`, `GrassSceneProxy.cpp` |
| Voronoi clumps that share height / facing / colour | `GrassFindClump` |
| Cubic Bezier blade, 15 / 7 vertex LODs | `EvaluateGrassBlade`, `FGrassBladeIndexBuffer` |
| Fewer, wider blades with distance | `SurviveLevel`, `Keep`, `WidthLodScale` |
| Rounded normals, view space thickening | `GrassVertexFactory.ush` |
| Wind from scrolling 2D noise plus per blade bobbing | `GrassValueNoise`, `EvaluateGrassBlade` |
| Grass does not render into shadow maps | component has `CastShadow = false` |

## What is different

- **LOD blending.** Every blade is identified by its cell on the LOD 0 grid, so a lower LOD tile
  creates exactly a subset of the blades of a higher LOD tile. Blades that do not exist in the next
  LOD shrink away with distance, which removes popping without a separate blend between tiles.
- **Mowing stripes** are physical: neighbouring stripes lean in opposite directions, so their
  brightness changes with the viewing angle like on a real pitch. `StripeContrast` adds a view
  independent difference on top.
- **Not implemented:** occlusion culling (a pitch is flat and nothing stands in front of it),
  grass type textures (the pitch is one type), far LOD terrain shading (put a ground plane with a
  matching colour under the grass) and artist authored wind and height maps.

## Using it

1. Build the project (the plugin has C++ and shaders).
2. Place a **Procedural Grass Field** actor and select it.
3. Press **Create Default Material** in the Details panel (Grass category). It builds `/Game/Grass/M_ProceduralGrass`
   in C++ (`GrassDefaultMaterial.cpp`) and assigns it. Then File > Save All.
   Keep the actor scale at 1; change **Field Size** instead.
4. Put a green plane under it. From far away the blades fade out and the plane is what you see.

### Making the ball and players press the grass

Add one **Grass Interactor** component to every actor that touches the grass (ball, character).
That does two things:

- **Exact fit (Conform To Objects).** The grass component captures the depth of those actors from
  below the pitch into a small render target, centred on the player. The compute shader reads, for
  every blade, how much room there is between the ground and the lowest surface above it, and tilts
  or removes the blade so it stays underneath. This works for any shape (a shoe, a ball, a sliding
  body) with no per-bone setup. Precision is Conform Area Size / Conform Resolution, about 2 cm by
  default. It only runs in game, not in the editor viewport.
- **Lean away.** Blades inside the interactor's Radius lean away from it. For the ball use a radius
  a little larger than the ball. For a character, one interactor per foot (attached to the foot
  bones through Parent Socket, radius about 12) gives a soft ring around each shoe.

### Ball that slows down on grass

Add **Grass Rolling Resistance** to the ball actor. While the ball touches a grass field it loses
speed at a constant rate plus a part proportional to its speed, scaled by the blade height.

### Console variables

- `r.ProceduralGrass.Enable 0/1`
- `r.ProceduralGrass.LodScale` (higher keeps more blades)
- `r.ProceduralGrass.MaxBladesHigh`, `r.ProceduralGrass.MaxBladesLow` (instance buffer capacity)

### Material inputs provided by the vertex factory

- Vertex Color RGB: root/tip gradient x brightness. Alpha: 0 at the root, 1 at the tip.
- TexCoord0: across / along the blade. TexCoord1: position on the field (0..1).
- TexCoord2: per blade random, stripe parity. TexCoord3: wind strength, blade height (cm).

The vertex factory compiles for materials with **Used with Virtual Heightfield Mesh** ticked.
A new usage flag would need an engine change, so that existing flag is borrowed.

## Next steps

- Persistent wear: accumulate the conform depth capture into a second render target that decays
  slowly, so slide marks stay and the pitch ages during a match (interaction today is momentary).
- Hi-Z occlusion culling once there are stands and players in front of the pitch.
- Velocity output for the wind motion.
