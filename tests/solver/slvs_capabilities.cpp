#include <hobbycad/sketch/solver.h>
#include <hobbycad/core.h>
#include <cstdio>
using namespace hobbycad;
int main(){
    std::printf("libhobbycad version              : %s\n", version());
    std::printf("Solver::isAvailable()            : %s\n", sketch::Solver::isAvailable()?"yes":"NO");
    std::printf("solverFatalHandlerAvailable()    : %s  (patch 0002)\n",
                sketch::solverFatalHandlerAvailable()?"yes":"NO");
    std::printf("solverCanRecoverFromFaults()     : %s  (patch 0003)\n",
                sketch::solverCanRecoverFromFaults()?"yes":"NO");
    bool ok = sketch::Solver::isAvailable()
           && sketch::solverFatalHandlerAvailable()
           && sketch::solverCanRecoverFromFaults();
    std::printf("\n%s\n", ok ? "patched libslvs is LIVE in this build"
                             : "NOT fully patched");
    return ok?0:1;
}
