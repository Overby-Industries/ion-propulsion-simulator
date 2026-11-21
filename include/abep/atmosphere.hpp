#pragma once

// A density profile given as a table of nodes, interpolated linearly in the
// logarithm of density: between two nodes the density falls exponentially.
// That reproduces an exact exponential atmosphere exactly and returns the nodes
// themselves unchanged.
//
// This header deliberately ships no table of real atmospheric data. A table is
// added only once its values have been checked against the published source;
// see docs/VALIDATION.md.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace abep {

class DensityTable {
public:
    struct Node {
        double altitude_m;
        double density_kg_m3;
    };

    // Altitudes must be strictly increasing and densities positive, with at
    // least two nodes. Throws std::invalid_argument otherwise.
    explicit DensityTable(std::vector<Node> nodes) : nodes_(std::move(nodes)) {
        if (nodes_.size() < 2) throw std::invalid_argument("DensityTable needs at least two nodes");
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (!(nodes_[i].density_kg_m3 > 0.0)) {
                throw std::invalid_argument("DensityTable densities must be positive");
            }
            if (i > 0 && !(nodes_[i].altitude_m > nodes_[i - 1].altitude_m)) {
                throw std::invalid_argument("DensityTable altitudes must be strictly increasing");
            }
        }
    }

    double min_altitude_m() const noexcept { return nodes_.front().altitude_m; }
    double max_altitude_m() const noexcept { return nodes_.back().altitude_m; }

    // Density at the given altitude. Throws std::out_of_range outside the table,
    // including for NaN: a satellite that has left the range the model covers
    // (reentry, say) should stop the run rather than get a made-up density.
    double density(double altitude_m) const {
        if (!(altitude_m >= min_altitude_m() && altitude_m <= max_altitude_m())) {
            throw std::out_of_range("altitude is outside the density table");
        }
        const auto above = std::upper_bound(
            nodes_.begin(), nodes_.end(), altitude_m,
            [](double h, const Node& n) { return h < n.altitude_m; });
        if (above == nodes_.end()) return nodes_.back().density_kg_m3;  // exactly the top node
        const Node& hi = *above;
        const Node& lo = *(above - 1);
        const double w = (altitude_m - lo.altitude_m) / (hi.altitude_m - lo.altitude_m);
        return lo.density_kg_m3 * std::exp(w * std::log(hi.density_kg_m3 / lo.density_kg_m3));
    }

private:
    std::vector<Node> nodes_;
};

}  // namespace abep
