// SPDX-License-Identifier: GPL-3.0-or-later
// Simple shapes drawn over the game's frame for mods (cursors, markers), and one picture.
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

    // A picture (RGBA, w x h, rows packed) drawn into the rectangle, turned `turns` quarter turns
    // clockwise, under the shapes. The pixels must stay valid until render; `generation`
    // changes when they change (it is uploaded again only then).
    void image(const uint8_t* rgba, int w, int h, uint64_t generation, float x, float y, float width, float height,
               int turns);

    // Host side: draws the queued shapes on a surface of the given size and clears the queue.
    void render(int width, int height);

private:
    struct Image {
        const uint8_t* rgba = nullptr;
        int w = 0, h = 0, turns = 0;
        uint64_t generation = 0;
        float x = 0, y = 0, width = 0, height = 0;
    };
    Image image_;
    void render_image(int width, int height);

    struct Shape {
        float x, y, outer, inner;
        uint32_t rgba;
    };
    std::vector<Shape> shapes_;
};

}  // namespace slingshot
