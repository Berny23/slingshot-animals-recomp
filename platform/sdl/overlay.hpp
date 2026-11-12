// SPDX-License-Identifier: GPL-3.0-or-later
// Simple shapes drawn over the game's frame for mods (cursors, markers).
// Everything is drawn after the game rendered and before the frame is
// presented; the game's GL state is restored afterwards, since the engine
// caches it.
#pragma once

#include <cstdint>
#include <vector>

namespace slingshot {

class Overlay {
public:
    // A filled ring (inner = 0: a disc). Positions in surface pixels, color 0xRRGGBBAA.
    void ring(float x, float y, float outer, float inner, uint32_t rgba);

    // Host side: draws the queued shapes on a surface of the given size and clears the queue.
    void render(int width, int height);

private:
    struct Shape {
        float x, y, outer, inner;
        uint32_t rgba;
    };
    std::vector<Shape> shapes_;
};

}  // namespace slingshot
