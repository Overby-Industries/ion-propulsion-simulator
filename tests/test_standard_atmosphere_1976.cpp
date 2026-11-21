#include "check.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/standard_atmosphere_1976.hpp"

#ifndef ABEP_DATA_DIR
#error "ABEP_DATA_DIR must name the repository's data directory; CMakeLists.txt sets it"
#endif

namespace {

namespace ussa = abep::ussa1976;
using abep::DensityTable;

constexpr double kRho0 = 1.2250;  // kg/m^3: the sea-level density the printed rho/rho_0 column is relative to

struct PrintedRow {
    double z_m;
    double rho;
    double rho_half_width;    // half a unit in the last printed figure
    double ratio;
    double ratio_half_width;
};

// Reads data/ussa1976_table1.txt in the report's own notation: "5.604-7" is 5.604e-7, and an exponent is given
// only where it changes. Written separately from tools/ussa1976_table.py, so the header, the data file and two
// independent readers must all agree.
bool readPrintedTable(const std::string& path, std::vector<PrintedRow>& rows) {
    std::ifstream in(path);
    if (!in) return false;
    int exponent[2] = {0, 0};
    bool known[2] = {false, false};
    std::string line;
    try {
        while (std::getline(in, line)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos || line[0] == '#') continue;
            std::istringstream fields(line);
            double z = 0.0;
            std::string token[2];
            if (!(fields >> z >> token[0] >> token[1])) return false;
            double value[2] = {0.0, 0.0};
            double half[2] = {0.0, 0.0};
            for (int k = 0; k < 2; ++k) {
                const std::size_t dash = token[k].find('-');
                if (dash != std::string::npos) {
                    exponent[k] = -std::stoi(token[k].substr(dash + 1));
                    known[k] = true;
                }
                if (!known[k]) return false;
                value[k] =
                    std::strtod((token[k].substr(0, dash) + "e" + std::to_string(exponent[k])).c_str(), nullptr);
                half[k] = 0.0005 * std::pow(10.0, exponent[k]);
            }
            rows.push_back({z, value[0], half[0], value[1], half[1]});
        }
    } catch (const std::exception&) {  // a malformed exponent: report it as an unreadable file, not a crash
        return false;
    }
    return true;
}

}  // namespace

void runStandardAtmosphere1976Tests() {
    const DensityTable table = ussa::density_table();

    {
        TEST("USSA76 table: 401 rows from 100 km to 1000 km, spaced as the report spaces them");
        CHECK_EQ(ussa::kRowCount, static_cast<std::size_t>(401));
        CHECK_EQ(table.min_altitude_m(), 100.0e3);
        CHECK_EQ(table.max_altitude_m(), 1000.0e3);
        for (std::size_t i = 0; i + 1 < ussa::kRowCount; ++i) {
            const double z = ussa::kRows[i].altitude_m;
            const double step = z < 300.0e3 ? 1.0e3 : (z < 500.0e3 ? 2.0e3 : 5.0e3);
            CHECK_EQ(ussa::kRows[i + 1].altitude_m - z, step);
        }
    }
    {
        TEST("USSA76 table: every row is returned unchanged");
        for (std::size_t i = 0; i < ussa::kRowCount; ++i) {
            CHECK_EQ(table.density(ussa::kRows[i].altitude_m), ussa::kRows[i].density_kg_m3);
        }
    }
    {
        TEST("USSA76 table: density falls with altitude, at a rate a real atmosphere could have");
        // A density scale height is a few km at 100 km and a few hundred km at 1000 km, so ln(rho) must fall by
        // between 0.003 and 0.2 per km everywhere. A slipped exponent (a factor of ten) would leave that range.
        // Bounds fixed before running.
        for (std::size_t i = 0; i + 1 < ussa::kRowCount; ++i) {
            const double dz_km = (ussa::kRows[i + 1].altitude_m - ussa::kRows[i].altitude_m) / 1.0e3;
            const double slope = -std::log(ussa::kRows[i + 1].density_kg_m3 / ussa::kRows[i].density_kg_m3) / dz_km;
            CHECK(slope >= 0.003 && slope <= 0.2);
        }
    }
    {
        TEST("USSA76 table: spot values typed in separately from the data file");
        // Printed in Table I of the report. Relative 1e-12 only allows for decimal-to-binary conversion.
        const struct { double km; double rho; } spot[] = {
            {100.0, 5.604e-7},  {150.0, 2.076e-9},  {200.0, 2.541e-10}, {250.0, 6.073e-11},
            {300.0, 1.916e-11}, {400.0, 2.803e-12}, {500.0, 5.215e-13}, {600.0, 1.137e-13},
            {700.0, 3.070e-14}, {800.0, 1.136e-14}, {900.0, 5.759e-15}, {1000.0, 3.561e-15},
        };
        for (const auto& s : spot) CHECK(std::fabs(table.density(s.km * 1.0e3) / s.rho - 1.0) < 1.0e-12);
    }
    {
        TEST("USSA76 table: the header matches the data file, and the printed rho and rho/rho_0 columns agree");
        std::vector<PrintedRow> printed;
        CHECK(readPrintedTable(std::string(ABEP_DATA_DIR) + "/ussa1976_table1.txt", printed));
        CHECK_EQ(printed.size(), ussa::kRowCount);
        for (std::size_t i = 0; i < printed.size() && i < ussa::kRowCount; ++i) {
            CHECK_EQ(printed[i].z_m, ussa::kRows[i].altitude_m);
            CHECK(std::fabs(printed[i].rho / ussa::kRows[i].density_kg_m3 - 1.0) < 1.0e-15);
            // The two printed columns are one quantity, scaled by rho_0, each rounded to four figures: they may
            // differ by their rounding and no more. 1e-9 relative only absorbs floating-point error.
            const double allowed = printed[i].rho_half_width + kRho0 * printed[i].ratio_half_width;
            CHECK(std::fabs(printed[i].rho - kRho0 * printed[i].ratio) <= allowed * (1.0 + 1.0e-9));
        }
    }
    {
        TEST("USSA76 table: interpolating between rows, measured by leaving every second row out");
        // The table keeps rows 0, 2, 4, ... and recovers the omitted odd rows, each exactly midway between its
        // neighbours. Log-linear interpolation errs by about step^2 / 8 times the curvature of ln(rho). The
        // curvature peaks near 120 km at about 5e-3 per km^2, so a gap of 2 km should err by about 0.25% at worst.
        // Tolerance fixed before running: 0.5% relative.
        std::vector<DensityTable::Node> kept;
        for (std::size_t i = 0; i < ussa::kRowCount; i += 2) kept.push_back(ussa::kRows[i]);
        const DensityTable thinned(kept);
        double worst = 0.0;
        double worst_km = 0.0;
        for (std::size_t i = 1; i + 1 < ussa::kRowCount; i += 2) {  // an omitted row always has a kept row above it
            const double error =
                std::fabs(thinned.density(ussa::kRows[i].altitude_m) / ussa::kRows[i].density_kg_m3 - 1.0);
            if (error > worst) {
                worst = error;
                worst_km = ussa::kRows[i].altitude_m / 1.0e3;
            }
            CHECK(error < 0.005);
        }
        std::printf("    worst error with every second row left out: %.3f%% at %.0f km\n", 100.0 * worst, worst_km);
    }
    {
        TEST("USSA76 table: outside 100 km to 1000 km is an error, not a guess");
        CHECK_THROWS(table.density(100.0e3 - 1.0), std::out_of_range);
        CHECK_THROWS(table.density(1000.0e3 + 1.0), std::out_of_range);
        CHECK_THROWS(table.density(0.0), std::out_of_range);
    }
}
