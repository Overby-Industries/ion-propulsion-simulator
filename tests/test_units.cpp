#include "check.hpp"

#include <initializer_list>

#include "abep/units.hpp"

void runUnitsTests() {
    using namespace abep::units;

    {
        TEST("thrust density: mN/kW to N/W");
        // 1 kW at 10 mN/kW is 10 mN, i.e. 0.01 N from 1000 W: 1e-5 N/W.
        CHECK_NEAR(mn_per_kw_to_n_per_w(10.0), 1.0e-5, 1.0e-18);
        CHECK_NEAR(mn_per_kw_to_n_per_w(13.7), 1.37e-5, 1.0e-18);
        // The conversion must reproduce the force it was derived from.
        CHECK_NEAR(mn_per_kw_to_n_per_w(10.0) * 1000.0, 0.010, 1.0e-15);
    }
    {
        TEST("thrust density: round trip");
        for (double v : {0.0, 1.0, 10.0, 13.7, 250.0}) {
            CHECK_NEAR(n_per_w_to_mn_per_kw(mn_per_kw_to_n_per_w(v)), v, 1.0e-9);
        }
    }
    {
        TEST("length");
        CHECK_NEAR(km_to_m(400.0), 400000.0, 1.0e-9);
        CHECK_NEAR(m_to_km(km_to_m(123.456)), 123.456, 1.0e-12);
    }
    {
        TEST("angle");
        CHECK_NEAR(deg_to_rad(180.0), kPi, 1.0e-15);
        CHECK_NEAR(deg_to_rad(90.0), kPi / 2.0, 1.0e-15);
        CHECK_NEAR(rad_to_deg(deg_to_rad(51.6)), 51.6, 1.0e-12);
    }
}
