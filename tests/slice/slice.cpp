// tests/slice/slice.cpp — mesh-plane slicing into boundary loops
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/brep/slice.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/brep/mesh_to_step.h>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <hobbycad/stl_io.h>
#include <hobbycad/step_io.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <string>
#include <array>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
using namespace hobbycad;
using namespace hobbycad::brep;
static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }

static std::array<gp_Pnt,3> T(double ax,double ay,double az,double bx,double by,double bz,double cx,double cy,double cz){
    return { gp_Pnt(ax,ay,az), gp_Pnt(bx,by,bz), gp_Pnt(cx,cy,cz) };
}

int main(){
    std::printf("mesh cross-section slicing\n");
    // unit cube 0..1, 12 triangles
    std::vector<std::array<gp_Pnt,3>> cube = {
        T(0,0,0, 1,0,0, 1,1,0), T(0,0,0, 1,1,0, 0,1,0),          // bottom
        T(0,0,1, 1,1,1, 1,0,1), T(0,0,1, 0,1,1, 1,1,1),          // top
        T(0,0,0, 0,0,1, 1,0,1), T(0,0,0, 1,0,1, 1,0,0),          // front y=0
        T(0,1,0, 1,1,1, 0,1,1), T(0,1,0, 1,1,0, 1,1,1),          // back  y=1
        T(0,0,0, 0,1,0, 0,1,1), T(0,0,0, 0,1,1, 0,0,1),          // left  x=0
        T(1,0,0, 1,1,1, 1,1,0), T(1,0,0, 1,0,1, 1,1,1),          // right x=1
    };
    gp_Pln plane(gp_Pnt(0,0,0.5), gp_Dir(0,0,1));   // cut at z=0.5
    auto loops = sliceTriangles(cube, plane);
    ck(loops.size() == 1, "a cube sliced at mid-height yields one loop");
    if (!loops.empty()) {
        const auto& L = loops[0];
        ck(L.closed, "the loop is closed");
        ck(L.points.size() >= 4, "the loop has at least the 4 corners");
        double minx=1e9,miny=1e9,maxx=-1e9,maxy=-1e9;
        for (auto& p : L.points){ minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);miny=std::min(miny,p.y);maxy=std::max(maxy,p.y);}
        ck(minx>-1e-6&&miny>-1e-6&&maxx>1-1e-6&&maxx<1+1e-6&&maxy>1-1e-6&&maxy<1+1e-6,
           "the section is the unit square [0,1]x[0,1]");
    }
    // no crossing above the cube -> no loops
    ck(sliceTriangles(cube, gp_Pln(gp_Pnt(0,0,2.0), gp_Dir(0,0,1))).empty(),
       "a plane above the mesh yields no loops");
    // ---- classifySection: outer/hole nesting (synthetic loops) -------------
    {
        auto square = [](double cx, double cy, double h, bool ccw){
            SliceLoop l; std::vector<Point2D> p =
                {{cx-h,cy-h},{cx+h,cy-h},{cx+h,cy+h},{cx-h,cy+h}};
            if(!ccw) std::reverse(p.begin(),p.end());
            l.points = p; return l;
        };
        // one outer with a hole inside it
        {
            std::vector<SliceLoop> ls = { square(0,0,10,true), square(0,0,4,false) };
            auto cs = classifySection(ls);
            ck(cs.size()==1, "outer+hole -> 1 contour");
            ck(cs.size()==1 && cs[0].holes.size()==1, "the contour has 1 hole");
        }
        // two disjoint outers, no holes
        {
            std::vector<SliceLoop> ls = { square(-20,0,5,true), square(20,0,5,true) };
            auto cs = classifySection(ls);
            ck(cs.size()==2, "two disjoint squares -> 2 contours");
            int hh=0; for(auto&c:cs) hh+=(int)c.holes.size();
            ck(hh==0, "disjoint squares have no holes");
        }
        // solid sitting inside a cavity: outer(20) > hole(12) > inner-solid(6)
        {
            std::vector<SliceLoop> ls = {
                square(0,0,20,true), square(0,0,12,false), square(0,0,6,true) };
            auto cs = classifySection(ls);
            ck(cs.size()==2, "solid-in-cavity -> 2 contours (outer + inner body)");
            // the big outer owns the cavity hole; the inner solid is its own body
            int totalHoles=0; for(auto&c:cs) totalHoles+=(int)c.holes.size();
            ck(totalHoles==1, "one hole total (the cavity), inner solid is a body");
        }
        // winding normalization: outer CCW, hole CW regardless of input winding
        {
            std::vector<SliceLoop> ls = { square(0,0,10,false), square(0,0,3,true) };
            auto cs = classifySection(ls);
            ck(cs.size()==1 && geometry::polygonArea(cs[0].outer.points)>0,
               "outer is normalized CCW");
            ck(cs.size()==1 && cs[0].holes.size()==1 &&
               geometry::polygonArea(cs[0].holes[0].points)<0,
               "hole is normalized CW");
        }
    }
    // ---- end-to-end: slice + loft the cube into a solid --------------------
    {
        auto res = loftMeshSections(cube, gp_Dir(0,0,1), 8);
        ck(res.success, "cube slices+lofts into a solid");
        ck(res.sections >= 2, "at least two section wires were lofted");
        if (res.success) {
            GProp_GProps props; BRepGProp::VolumeProperties(res.shape, props);
            const double vol = props.Mass();
            std::printf("    loft: sections=%d volume=%.4f\n", res.sections, vol);
            ck(vol > 0.3 && vol < 1.6, "the lofted solid has a plausible volume (~1 for a unit cube)");
        }
    }
    // ---- full public path: box shape -> STL file -> stlToStep -> read STEP --
    {
        TopoDS_Shape box = BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape();
        BRepMesh_IncrementalMesh mesher(box, 0.05); mesher.Perform();
        const std::string stl  = "/tmp/hobbycad_m2s_box.stl";
        const std::string step = "/tmp/hobbycad_m2s_box.step";
        auto wr = hobbycad::stl_io::writeStl(stl, { box });
        ck(wr.success, "wrote a box STL file");
        std::string cerr;
        bool ok = hobbycad::brep::stlToStep(stl, step, gp_Dir(0,0,1), 24, &cerr);
        if (!ok) std::printf("    >> stlToStep err: %s\n", cerr.c_str());
        ck(ok, "stlToStep converts the STL to a STEP file");
        auto rr = hobbycad::step_io::readStep(step);
        ck(rr.success && !rr.shapes.empty(), "the STEP reads back with shapes");
        if (!rr.shapes.empty()) {
            GProp_GProps pr; BRepGProp::VolumeProperties(rr.shapes[0], pr);
            const double vol = pr.Mass();
            std::printf("    >> round-trip STEP volume=%.3f (box 2^3 = 8)\n", vol);
            ck(vol > 4.0 && vol < 9.0, "round-trip STEP solid has ~box volume");
        }
    }
    std::printf("%s\n", fails? "FAILED":"OK");
    return fails?1:0;
}
