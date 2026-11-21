#include "check.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "abep/atmosphere.hpp"

void runAtmosphereTests() {
    using abep::DensityTable;
    using Node = abep::DensityTable::Node;

    // An exact exponential atmosphere, sampled at nodes. These numbers are arithmetic scaffolding for
    // testing the interpolation, not atmospheric data.
    const double h0 = 100.0e3;
    const double rho0 = 5.0e-7;
    const double scale_height = 40.0e3;
    const auto exact = [&](double h) { return rho0 * std::exp(-(h - h0) / scale_height); };
    std::vector<Node> nodes;
    for (double h = h0; h <= 500.0e3; h += 50.0e3) nodes.push_back({h, exact(h)});
    const DensityTable table(nodes);

    {
        TEST("density table: an exact exponential is reproduced between the nodes");
        // Log-linear interpolation is exact for an exponential, so the only error is rounding.
        // Tolerance fixed before running: 1e-12 relative.
        for (double h = h0; h <= 500.0e3; h += 3137.0) {
            CHECK(std::fabs(table.density(h) / exact(h) - 1.0) < 1.0e-12);
        }
    }
    {
        TEST("density table: the nodes are returned unchanged");
        for (const Node& n : nodes) CHECK_EQ(table.density(n.altitude_m), n.density_kg_m3);
    }
    {
        TEST("density table: continuous across a node");
        const double h = 250.0e3;
        CHECK(std::fabs(table.density(h - 1.0e-6) / table.density(h + 1.0e-6) - 1.0) < 1.0e-9);
    }
    {
        TEST("density table: outside the range is an error, not a guess");
        CHECK_THROWS(table.density(99.0e3), std::out_of_range);
        CHECK_THROWS(table.density(500.0e3 + 1.0), std::out_of_range);
        CHECK_THROWS(table.density(std::numeric_limits<double>::quiet_NaN()), std::out_of_range);
        CHECK_EQ(table.min_altitude_m(), h0);
        CHECK_EQ(table.max_altitude_m(), 500.0e3);
    }
    {
        TEST("density table: a malformed table is rejected");
        CHECK_THROWS(DensityTable(std::vector<Node>{{h0, rho0}}), std::invalid_argument);
        CHECK_THROWS(DensityTable(std::vector<Node>{{h0, rho0}, {h0, rho0 / 2.0}}), std::invalid_argument);
        CHECK_THROWS(DensityTable(std::vector<Node>{{2.0 * h0, rho0}, {h0, rho0 / 2.0}}), std::invalid_argument);
        CHECK_THROWS(DensityTable(std::vector<Node>{{h0, rho0}, {2.0 * h0, 0.0}}), std::invalid_argument);
        CHECK_THROWS(DensityTable(std::vector<Node>{{h0, -rho0}, {2.0 * h0, rho0}}), std::invalid_argument);
    }
}
