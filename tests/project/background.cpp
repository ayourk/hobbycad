// tests/project/background.cpp — BackgroundImage bounds/containsPoint honor rotation.
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/sketch/background.h>
#include <cmath>
#include <cstdio>
using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }
static bool near(double a, double b){ return std::fabs(a-b) < 1e-6; }
int main() {
    {   // the picture's scale: mm per pixel, uniform setter, locked proportions
        hobbycad::sketch::BackgroundImage bg;
        bg.originalPixelWidth = 3000; bg.originalPixelHeight = 2000;
        bg.setMmPerPixel(25.4 / 300.0);                  // a 300 DPI scan
        ck(near(bg.width, 254.0) && near(bg.height, 169.33333333), "setMmPerPixel sizes from the pixel dimensions");
        ck(near(bg.mmPerPixelX(), 25.4 / 300.0) && near(bg.mmPerPixelY(), 25.4 / 300.0), "mmPerPixel reads back");
        ck(bg.hasUniformScale(), "one scale on both axes");
        ck(near(bg.heightForWidthLocked(300.0), 200.0) && near(bg.widthForHeightLocked(100.0), 150.0),
           "locked proportions follow the pixel size");
        bg.height = 300.0;                                // stretched with the lock off
        ck(!bg.hasUniformScale(), "a stretched picture has two scales");
        bg.setMmPerPixel(0.0);
        ck(near(bg.height, 300.0), "a non-positive scale is ignored");
        hobbycad::sketch::BackgroundImage unknown;        // no pixel size yet
        unknown.width = 100; unknown.height = 50;
        ck(unknown.mmPerPixelX() == 0.0 && unknown.hasUniformScale(), "unknown pixel size: scale 0, nothing to disagree");
        ck(near(unknown.heightForWidthLocked(200.0), 100.0), "and the lock falls back to the current proportions");
    }
    std::printf("BackgroundImage rotation\n");
    sketch::BackgroundImage bg;
    bg.enabled = true; bg.position = {0,0}; bg.width = 10; bg.height = 20; bg.rotation = 0;

    { auto b = bg.bounds();
      ck(near(b.minX,0)&&near(b.minY,0)&&near(b.maxX,10)&&near(b.maxY,20), "unrotated bounds"); }
    ck(bg.containsPoint({5,5}),  "unrotated contains an interior point");
    ck(!bg.containsPoint({100,100}), "unrotated excludes a far point");

    bg.rotation = 90;   // 10x20 rect turned 90deg about center (5,10)
    { auto b = bg.bounds();
      ck(near(b.minX,-5)&&near(b.minY,5)&&near(b.maxX,15)&&near(b.maxY,15),
         "90deg bounds = the rotated AABB (-5,5)-(15,15)"); }
    ck(bg.containsPoint({5,10}),  "90deg contains the center");
    ck(!bg.containsPoint({0,0}),  "90deg excludes a point outside the rotated rect");
    ck(bg.containsPoint({13,10}), "90deg contains a point in the rotated extent (x>10, was outside un-rotated)");

    if (fails==0) std::printf("background: ALL PASS\n"); else std::printf("background: %d FAIL\n", fails);
    return fails ? 1 : 0;
}
