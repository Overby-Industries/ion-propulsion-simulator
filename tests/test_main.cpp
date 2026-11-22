#include "check.hpp"

int main() {
    std::printf("ABEP: Ion Thrusters Simulator tests\n");
    runVersionTests();
    runUnitsTests();
    runConstantsTests();
    runOrbitTests();
    runDragTests();
    runAtmosphereTests();
    runStandardAtmosphere1976Tests();
    runThrustTests();
    runEclipseTests();
    runPowerTests();
    runRomano2018Tests();
    runThermalTests();
    runFlightTests();
    runMissionTests();
    std::printf("\n%d checks, %d failures\n", check::checks(), check::failures());
    return check::failures() == 0 ? 0 : 1;
}
