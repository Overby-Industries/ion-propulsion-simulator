#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/drag.hpp"
#include "abep/orbit.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/thrust.hpp"

#ifndef ABEP_DATA_DIR
#error "ABEP_DATA_DIR must name the repository's data directory; CMakeLists.txt sets it"
#endif

// Validation case 3: the simulator's drag, and its intake mass flow, against curves published by F. Romano et al.,
// Acta Astronautica 147 (2018) 114-126, for a satellite with a frontal area of 1 m^2 and C_D = 2.2 in an NRLMSISE-00
// atmosphere at average solar activity. The curves are in data/romano2018_curves.csv, read from the paper's vector
// figures by tools/verification/digitize_romano2018.py.

namespace {

using abep::DensityTable;

struct Row {
    double altitude_km = 0.0;
    double density = 0.0;      // kg/m^3, Fig. 3, F10.7 = 140
    double drag_mn = 0.0;      // mN, Fig. 6
    double flow_mg_s[3] = {0.0, 0.0, 0.0};  // Fig. 5, collection efficiency 0.35, 0.46, 0.90
};

constexpr double kEtas[3] = {0.35, 0.46, 0.90};
constexpr double kCd = 2.2;     // the paper's drag coefficient
constexpr double kArea = 1.0;   // m^2, the paper's frontal area and intake area

bool readCurves(const std::string& path, std::vector<Row>& rows) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool header_seen = false;
    try {
        while (std::getline(in, line)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos || line[0] == '#') continue;
            if (!header_seen) {  // the column names
                header_seen = true;
                continue;
            }
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream fields(line);
            Row r;
            if (!(fields >> r.altitude_km >> r.density >> r.drag_mn >> r.flow_mg_s[0] >> r.flow_mg_s[1] >> r.flow_mg_s[2])) {
                return false;
            }
            rows.push_back(r);
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// A satellite in a circular equatorial prograde orbit at the given altitude.
abep::State orbit_at(double altitude_m) { return abep::circular_orbit_state(altitude_m, 0.0); }

}  // namespace

void runRomano2018Tests() {
    std::vector<Row> rows;
    const bool read = readCurves(std::string(ABEP_DATA_DIR) + "/romano2018_curves.csv", rows);
    CHECK(read);
    CHECK_EQ(rows.size(), static_cast<std::size_t>(14));  // 120 to 250 km every 10 km
    if (!read || rows.size() != 14) return;

    std::vector<DensityTable::Node> nodes;
    for (const Row& r : rows) nodes.push_back({r.altitude_km * 1.0e3, r.density});
    const DensityTable paper_density(nodes);  // every query below is at one of its own nodes: no interpolation
    const DensityTable ussa = abep::ussa1976::density_table();

    {
        TEST("published curves: the data are in order and fall with altitude");
        for (std::size_t i = 0; i + 1 < rows.size(); ++i) {
            CHECK(rows[i + 1].altitude_km - rows[i].altitude_km == 10.0);
            CHECK(rows[i + 1].density < rows[i].density);
            CHECK(rows[i + 1].drag_mn < rows[i].drag_mn);
            for (int k = 0; k < 3; ++k) CHECK(rows[i + 1].flow_mg_s[k] < rows[i].flow_mg_s[k]);
        }
    }
    {
        TEST("drag: the simulator, given the paper's density, reproduces the paper's drag curve");
        // Tolerance fixed before running: 3%. The curve was read from the PDF to about 0.1%. The paper does not say
        // how it took the speed relative to the air; the simulator's co-rotating atmosphere (v - w r in an equatorial
        // orbit) and a surface-speed form (v - w R_E) differ by at most w h / v = 0.24% in speed, 0.5% in drag.
        double worst = 0.0;
        double worst_km = 0.0;
        double worst_non_rotating = 0.0;
        for (const Row& r : rows) {
            const abep::State s = orbit_at(r.altitude_km * 1.0e3);
            CHECK_NEAR(abep::altitude_above_ellipsoid(s.r), r.altitude_km * 1.0e3, 1.0e-6);  // equatorial: altitude is exact
            const double rho = paper_density.density(r.altitude_km * 1.0e3);
            const double published = r.drag_mn * 1.0e-3;  // N
            // with a satellite of 1 kg the acceleration in m/s^2 is the force in N
            const double mine = abep::norm(abep::drag_acceleration(s, rho, kCd, kArea, 1.0, true));
            const double error = std::fabs(mine / published - 1.0);
            CHECK(error < 0.03);
            if (error > worst) {
                worst = error;
                worst_km = r.altitude_km;
            }
            const double plain = abep::norm(abep::drag_acceleration(s, rho, kCd, kArea, 1.0, false));
            worst_non_rotating = std::max(worst_non_rotating, std::fabs(plain / published - 1.0));
        }
        std::printf("    worst difference from the published curve: %.2f%% at %.0f km\n", 100.0 * worst, worst_km);
        // The test can tell conventions apart: a non-rotating atmosphere would be about 13% off, far outside 3%.
        // (This is the first attempt, recorded in docs/VALIDATION.md, finding 10.)
        CHECK(worst_non_rotating > 0.10);
        std::printf("    a non-rotating atmosphere would be out by up to %.1f%%\n", 100.0 * worst_non_rotating);
    }
    {
        TEST("intake: the simulator's collectable mass flow reproduces the paper's, for three collection efficiencies");
        // Tolerance fixed before running: 3%, for the same reasons as the drag curve.
        double worst = 0.0;
        for (int k = 0; k < 3; ++k) {
            const abep::IntakeThruster t{1.37e-5, 20.0e3, kArea, kEtas[k]};  // tau and c_e do not enter the mass flow
            for (const Row& r : rows) {
                const abep::State s = orbit_at(r.altitude_km * 1.0e3);
                const double rho = paper_density.density(r.altitude_km * 1.0e3);
                const double mine = abep::collectable_mass_flow(t, rho, abep::norm(abep::velocity_relative_to_air(s, true))) * 1.0e6;  // mg/s
                const double error = std::fabs(mine / r.flow_mg_s[k] - 1.0);
                CHECK(error < 0.03);
                worst = std::max(worst, error);
            }
        }
        std::printf("    worst difference from the published curves: %.2f%%\n", 100.0 * worst);
    }
    {
        TEST("drag: in the 1976 standard atmosphere it lies within a factor of two of the published curve");
        // A sanity band, not a validation: the two atmospheres differ (USSA76 is one fixed profile, NRLMSISE-00 here is
        // for F10.7 = 140 at an unstated place and time), so agreement is not expected to be close; a factor of two
        // would catch a slip of units or a missing factor. Band fixed before running: 0.5 to 2.
        double lowest = 1.0e9;
        double highest = 0.0;
        for (const Row& r : rows) {
            const abep::State s = orbit_at(r.altitude_km * 1.0e3);
            const double rho = ussa.density(r.altitude_km * 1.0e3);
            const double ratio = abep::norm(abep::drag_acceleration(s, rho, kCd, kArea, 1.0, true)) / (r.drag_mn * 1.0e-3);
            CHECK(ratio > 0.5 && ratio < 2.0);
            lowest = std::min(lowest, ratio);
            highest = std::max(highest, ratio);
        }
        std::printf("    with the 1976 standard atmosphere the drag is %.2f to %.2f times the published curve\n", lowest, highest);
    }
}
