// SPDX-License-Identifier: AGPL-3.0-or-later
// render_preview.cpp - draw the controller screen on a PC and write it out
// as PPM images, one per scene, so the layout can be checked without the
// hardware. Uses the real screen.cpp, canvas.cpp, theme.cpp and font5x7.c;
// only present() is replaced.
//
// Build and run from the project root: test/host/render_preview.sh <outdir>

#include <cstdio>
#include <string>

#include "display.hpp"
#include "display_frame.hpp"
#include "screen.hpp"

namespace display {
bool present(int, int, int, int) { return true; }
}  // namespace display

namespace {
void save(const std::string &dir, const char *name) {
    const std::string path = dir + "/" + name + ".ppm";
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::perror(path.c_str());
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", display::kWidth, display::kHeight);
    const uint8_t *p = display::detail::frame();
    for (int i = 0; i < display::kWidth * display::kHeight; i++) {
        const unsigned v = (static_cast<unsigned>(p[2 * i]) << 8) | p[2 * i + 1];
        const unsigned char rgb[3] = {
            static_cast<unsigned char>(((v >> 11) & 0x1Fu) * 255u / 31u),
            static_cast<unsigned char>(((v >> 5) & 0x3Fu) * 255u / 63u),
            static_cast<unsigned char>((v & 0x1Fu) * 255u / 31u),
        };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

void scene(const std::string &dir, const char *name, const screen::Model &m, int frames) {
    screen::reset();
    for (int i = 0; i < frames; i++) screen::step(m);
    screen::render(m);
    save(dir, name);
}
}  // namespace

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    screen::Model m;
    m.power = 60;
    m.led = 6;

    m.link = screen::Link::Scanning;
    scene(dir, "1_scanning", m, 3);

    m.link = screen::Link::Ready;
    m.port[0].kind = screen::PortKind::Motor;
    scene(dir, "2_motor_stopped", m, 0);

    m.port[0].power = 60;
    m.drive = 1;
    m.locked = true;
    m.port[1].kind = screen::PortKind::Tilt;
    m.port[1].has_value = true;
    m.port[1].tilt_x = -12;
    m.port[1].tilt_y = 30;
    m.led = 1;
    m.note = "C5";
    m.tone_playing = true;
    scene(dir, "3_motor_fwd_tilt_tone", m, 5);

    m.port[0].power = -100;
    m.drive = -1;
    m.locked = false;
    m.power = 100;
    m.port[1].kind = screen::PortKind::Motion;
    m.port[1].level = 87;
    m.led = 7;
    m.tone_playing = false;
    scene(dir, "4_motor_rev_motion", m, 9);

    m.port[0] = screen::Port();
    m.port[0].kind = screen::PortKind::Unknown;
    m.port[0].type_id = 37;
    m.port[1] = screen::Port();
    m.port[1].kind = screen::PortKind::Tilt;
    m.drive = 0;
    m.power = 10;
    m.led = 0;
    m.note = nullptr;
    scene(dir, "5_unknown_waiting_off", m, 0);
    return 0;
}
