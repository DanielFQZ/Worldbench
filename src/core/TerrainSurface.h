#pragma once
#include "RenderSupport.h"
#include <atomic>
#include <functional>
#include <span>

namespace wb {
// A regular lattice of column-centre heights. Finest RTIN triangles have a
// checkerboard diagonal: (x+z) even uses NW-SE, odd uses NE-SW. All coarser
// triangles are unions of these reference triangles (no filtering of samples).
struct SurfaceGrid {
    int size = 0; // power-of-two interval count; (size+1)^2 samples
    std::vector<float> heights;
    int clipWidth = 0, clipDepth = 0; // zero means size; padded rectangular patch
};
struct SurfaceTopology {
    std::vector<uint32_t> indices; // indices into the original sample lattice
    float certifiedError = 0;
};
SurfaceTopology triangulateSurface(const SurfaceGrid &grid, float tolerance, bool fullBoundary = true);
// Full-resolution source samples feed the error-bounded terrain surface.
// Patches own sample intervals; adjacent patches share their endpoint samples.
std::vector<FarTile> planSurfaceTiles(const World &world, int patchSize = 128);
// Gesture-only approximation. Interior uses sparse samples, perimeter remains
// per-column to match unchanged reference patches. Never export/certify this.
MeshPtr buildInteractiveSurfaceMesh(const World &world, const FarTile &tile,
                                    const SurfaceMaterials &inputs, uint64_t revision,
                                    const std::atomic_bool *cancelled = nullptr, int stride = 8);
MeshPtr buildSurfaceMesh(const World &world, const FarTile &tile, const SurfaceMaterials &inputs,
                         uint64_t revision, float tolerance = .5f,
                         const std::atomic_bool *cancelled = nullptr, const QRect &exactArea = {});
} // namespace wb
