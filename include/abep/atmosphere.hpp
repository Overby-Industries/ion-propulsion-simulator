#pragma once

// A density profile given as a table of nodes. Between two nodes the density is found in one of two ways (Interpolation): linear
// in the logarithm of density, so that it falls exponentially from one node to the next, which reproduces an exact exponential
// atmosphere exactly and returns the nodes themselves unchanged; or a monotone cubic of the logarithm, for tables whose nodes lie
// far apart (the 20 km rows of ECSS-E-ST-10-04C, docs/VALIDATION.md case 31).
//
// This header deliberately ships no table of real atmospheric data. A table is added only once its values have been checked
// against the published source; see docs/VALIDATION.md.

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

    enum class Interpolation {
        // ln(density) linear in altitude between nodes. The default, and what the 1976 standard's 1 to 5 km rows use.
        LogLinear,
        // A monotone cubic (Fritsch and Carlson) of ln(density): smooth, never rises where the nodes fall, and exact for an
        // exponential atmosphere. For nodes far apart.
        MonotoneCubic,
    };

    // Altitudes must be strictly increasing and densities positive, with at least two nodes. Throws std::invalid_argument
    // otherwise.
    explicit DensityTable(std::vector<Node> nodes, Interpolation interpolation = Interpolation::LogLinear)
        : nodes_(std::move(nodes)), interpolation_(interpolation) {
        if (nodes_.size() < 2) throw std::invalid_argument("DensityTable needs at least two nodes");
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (!(nodes_[i].density_kg_m3 > 0.0)) {
                throw std::invalid_argument("DensityTable densities must be positive");
            }
            if (i > 0 && !(nodes_[i].altitude_m > nodes_[i - 1].altitude_m)) {
                throw std::invalid_argument("DensityTable altitudes must be strictly increasing");
            }
        }
        if (interpolation_ == Interpolation::MonotoneCubic) prepare_cubic();
    }

    double min_altitude_m() const noexcept { return nodes_.front().altitude_m; }
    double max_altitude_m() const noexcept { return nodes_.back().altitude_m; }
    Interpolation interpolation() const noexcept { return interpolation_; }

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
        if (interpolation_ == Interpolation::MonotoneCubic) {
            return cubic_density(static_cast<std::size_t>(above - nodes_.begin()) - 1, altitude_m);
        }
        const Node& hi = *above;
        const Node& lo = *(above - 1);
        const double w = (altitude_m - lo.altitude_m) / (hi.altitude_m - lo.altitude_m);
        return lo.density_kg_m3 * std::exp(w * std::log(hi.density_kg_m3 / lo.density_kg_m3));
    }

private:
    // The slopes of ln(density) at the nodes, by the weighted harmonic mean of the neighbouring secants where they agree in sign
    // and zero where they do not (a turning point); the ends by a one-sided three-point formula that is kept from overshooting.
    void prepare_cubic() {
        const std::size_t n = nodes_.size();
        ln_.resize(n);
        slope_.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) ln_[i] = std::log(nodes_[i].density_kg_m3);
        std::vector<double> width(n - 1);
        std::vector<double> secant(n - 1);
        for (std::size_t i = 0; i + 1 < n; ++i) {
            width[i] = nodes_[i + 1].altitude_m - nodes_[i].altitude_m;
            secant[i] = (ln_[i + 1] - ln_[i]) / width[i];
        }
        if (n == 2) {
            slope_[0] = slope_[1] = secant[0];
            return;
        }
        for (std::size_t i = 1; i + 1 < n; ++i) {
            if (secant[i - 1] * secant[i] > 0.0) {
                const double w1 = 2.0 * width[i] + width[i - 1];
                const double w2 = width[i] + 2.0 * width[i - 1];
                slope_[i] = (w1 + w2) / (w1 / secant[i - 1] + w2 / secant[i]);
            }
        }
        slope_[0] = end_slope(width[0], width[1], secant[0], secant[1]);
        slope_[n - 1] = end_slope(width[n - 2], width[n - 3], secant[n - 2], secant[n - 3]);
    }

    static double end_slope(double w0, double w1, double s0, double s1) {
        const double s = ((2.0 * w0 + w1) * s0 - w0 * s1) / (w0 + w1);
        if (s * s0 <= 0.0) return 0.0;
        if (s0 * s1 <= 0.0 && std::fabs(s) > 3.0 * std::fabs(s0)) return 3.0 * s0;
        return s;
    }

    // The cubic Hermite polynomial of ln(density) on the interval that starts at node i.
    double cubic_density(std::size_t i, double altitude_m) const {
        const Node& lo = nodes_[i];
        const double width = nodes_[i + 1].altitude_m - lo.altitude_m;
        const double t = (altitude_m - lo.altitude_m) / width;
        if (t == 0.0) return lo.density_kg_m3;  // exactly a node: returned as it was given
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double ln_density = (2.0 * t3 - 3.0 * t2 + 1.0) * ln_[i] + (t3 - 2.0 * t2 + t) * width * slope_[i] +
                                  (3.0 * t2 - 2.0 * t3) * ln_[i + 1] + (t3 - t2) * width * slope_[i + 1];
        return std::exp(ln_density);
    }

    std::vector<Node> nodes_;
    Interpolation interpolation_;
    std::vector<double> ln_;     // ln(density) at the nodes, for MonotoneCubic only
    std::vector<double> slope_;  // d ln(density) / d altitude at the nodes, for MonotoneCubic only
};

}  // namespace abep
