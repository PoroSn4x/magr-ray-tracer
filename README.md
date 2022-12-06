GPU-based ray tracer for the course Advanced Graphics (INFOMAGR) at Utrecht University.
By
Tariq Bakhtali (5631394)
Merijn Schepers (6504477)

Original template by bikker.j@gmail.com.

### Tasks:
- [x] An extendible material class.
- [x] Implementing a generic and extendible architecture for a ray tracer (mostly already there if
you go for the ray tracing template).
- [x] A ‘free camera’ with configurable position, orientation, FOV and aspect ratio. ‘Configurable’
means: adjustable from code, or at runtime using some interface.
- [x] A basic user interface, or keys / mouse input handling to control the camera position and
orientation at run-time.
- [x] Support for (at least) planes, spheres and triangles.
- [x] A basic scene, consisting of a small set of these primitives. Use each primitive type at least
once.
- [ ] A Whitted-style ray tracing renderer, supporting shadows, reflections and refraction with
absorption (Beer’s law).
- [ ] A Kajiya-style path tracing renderer, supporting diffuse interreflections and area lights.

### Bonus:
- [x] Support for triangle meshes of arbitrary size, using ‘obj’, ‘gltf’ or ‘fbx’ files to import scenes
(max 1pt). The .obj file format is highly recommended for this assignment.
- [ ] Support for complex primitives - complex being a torus or better (max 1pt).
- [ ] Texturing on all supported primitives. Texture must be a bitmap loaded from a file (max 1pt).
- [ ] Besides the point lights: Spot lights and directional lights (max 0.5 pts).
- [ ] Anti-aliasing (max 0.5 pts).
- [x] A skydome, with a texture loaded from a HDR file format (max 0.5 pts).
- [ ] Barrel distortion, a Panini-projection and/or a fish eye lens (max 0.5 pts).
- [x] Image postprocessing: gamma correction, vignetting and chromatic aberration (max 0.5 pts).
