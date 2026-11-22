"""Tests of the abep Python module.

ctest runs this when the module is built (cmake -DABEP_BUILD_PYTHON=ON). By hand: put the folder that holds the built module
(abep.*.so or abep.*.pyd) on PYTHONPATH, and run   python python/test_abep.py
"""

import csv
import os
import tempfile
import unittest

import abep

MODES = {"SAFE", "CHARGE", "COAST", "THRUST", "FAULT"}
SAMPLE_COLUMNS = [
    "t_s", "mode", "altitude_km", "density_kg_m3", "relative_speed_m_s", "drag_n", "thrust_n", "net_drag_n",
    "thruster_power_w", "generation_w", "load_w", "battery_wh", "temperature_k", "sunlit",
]
ORBIT_COLUMNS = [
    "orbit", "duration_s", "thrust_s", "duty", "eclipse_s", "generated_wh", "load_wh", "thruster_wh",
    "battery_change_wh", "spilled_wh",
]


class SettingsTest(unittest.TestCase):
    def test_every_setting_has_a_default(self):
        s = abep.settings()
        self.assertEqual(len(s), 36)
        self.assertEqual(s["altitude_km"], 250.0)
        self.assertEqual(s["include_j2"], 1.0)
        self.assertEqual(s["thrust_density_mn_per_kw"], 13.7)
        for name, value in s.items():
            self.assertIsInstance(value, float, name)

    def test_the_version_is_there(self):
        self.assertRegex(abep.__version__, r"^\d+\.\d+\.\d+$")


class RunTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result = abep.run(duration_days=0.25)

    def test_the_three_parts(self):
        self.assertEqual(sorted(self.result), ["orbits", "samples", "summary"])

    def test_samples_are_columns_of_the_same_length(self):
        samples = self.result["samples"]
        self.assertEqual(list(samples), SAMPLE_COLUMNS)  # in the order of the CSV
        lengths = {len(v) for v in samples.values()}
        self.assertEqual(len(lengths), 1)
        n = lengths.pop()
        self.assertGreater(n, 100)
        self.assertTrue(set(samples["mode"]) <= MODES)
        self.assertTrue(set(samples["sunlit"]) <= {0, 1})
        self.assertEqual(samples["t_s"][0], 0.0)
        self.assertTrue(all(200.0 < h < 300.0 for h in samples["altitude_km"]))
        self.assertTrue(all(d > 0.0 for d in samples["density_kg_m3"]))

    def test_orbits(self):
        orbits = self.result["orbits"]
        self.assertEqual(list(orbits), ORBIT_COLUMNS)
        self.assertGreaterEqual(len(orbits["orbit"]), 4)
        self.assertEqual(orbits["orbit"], list(range(len(orbits["orbit"]))))
        self.assertTrue(all(0.0 <= d <= 1.0 for d in orbits["duty"]))
        self.assertAlmostEqual(sum(orbits["duration_s"]), 0.25 * 86400.0, places=6)

    def test_summary(self):
        s = self.result["summary"]
        self.assertEqual(s["end_reason"], "Completed")
        self.assertEqual(s["elapsed_s"], 21600.0)
        self.assertEqual(s["hold_status"], "Holds")
        self.assertEqual(s["decayed"], 1.0)
        self.assertTrue(0.1 < s["thrust_duty"] < 0.9)
        total = sum(s["seconds_in_" + m] for m in MODES)
        self.assertAlmostEqual(total, s["elapsed_s"], places=6)

    def test_the_held_power_is_the_drag_over_the_thrust_density(self):
        # The default intake is large, so the thrust is power-limited all round the orbit and the solution is exactly this.
        s = self.result["summary"]
        tau = 13.7e-6
        self.assertAlmostEqual(s["hold_power_w"] * tau, s["hold_mean_drag_n"], delta=1e-9 * s["hold_mean_drag_n"])

    def test_the_same_inputs_give_the_same_run(self):
        self.assertEqual(abep.run(duration_days=0.1), abep.run(duration_days=0.1))

    def test_settings_change_the_answer(self):
        light = abep.run(duration_days=0.05, mass_kg=100)["summary"]["decay_time_days"]
        heavy = abep.run(duration_days=0.05, mass_kg=200)["summary"]["decay_time_days"]
        self.assertAlmostEqual(heavy / light, 2.0, delta=0.05)  # the decay time goes with the mass

    def test_a_drag_only_run_never_thrusts(self):
        r = abep.run(duration_days=0.1, thruster_enabled=False)
        self.assertEqual(set(r["samples"]["thrust_n"]), {0.0})
        self.assertEqual(r["summary"]["thrust_duty"], 0.0)

    def test_a_bool_is_a_zero_or_a_one(self):
        self.assertEqual(abep.run(duration_days=0.05, include_j2=False), abep.run(duration_days=0.05, include_j2=0))


class ErrorsTest(unittest.TestCase):
    def test_an_unknown_setting_is_a_value_error_naming_it(self):
        with self.assertRaisesRegex(ValueError, "altitude"):
            abep.run(altitude=300)

    def test_a_value_that_makes_no_sense_is_a_value_error(self):
        with self.assertRaises(ValueError):
            abep.run(mass_kg=0)
        with self.assertRaises(ValueError):
            abep.run(include_j2=2)
        with self.assertRaises(ValueError):
            abep.run(sample_every=2.5)

    def test_something_that_is_not_a_number_is_a_type_error(self):
        with self.assertRaises(TypeError):
            abep.run(altitude_km="high")
        with self.assertRaises(TypeError):
            abep.run(altitude_km=None)

    def test_a_number_that_is_not_finite_is_refused(self):
        with self.assertRaises(ValueError):
            abep.run(altitude_km=float("nan"))
        with self.assertRaises(ValueError):
            abep.run(altitude_km=float("inf"))


class CsvTest(unittest.TestCase):
    def test_the_files_read_back_as_the_same_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            target = os.path.join(directory, "run")
            self.assertEqual(abep.write_csv(target, duration_days=0.1, sample_every=3), target)
            self.assertEqual(sorted(os.listdir(target)), ["orbits.csv", "samples.csv", "summary.csv"])
            result = abep.run(duration_days=0.1, sample_every=3)

            with open(os.path.join(target, "samples.csv"), newline="") as f:
                rows = list(csv.DictReader(f))
            samples = result["samples"]
            self.assertEqual(len(rows), len(samples["t_s"]))
            for column in SAMPLE_COLUMNS:
                if column == "mode":
                    self.assertEqual([r[column] for r in rows], samples[column])
                elif column == "sunlit":
                    self.assertEqual([int(r[column]) for r in rows], samples[column])
                else:
                    self.assertEqual([float(r[column]) for r in rows], samples[column], column)  # 17 digits: exact

            with open(os.path.join(target, "orbits.csv"), newline="") as f:
                rows = list(csv.DictReader(f))
            for column in ORBIT_COLUMNS:
                self.assertEqual([float(r[column]) for r in rows], [float(x) for x in result["orbits"][column]], column)

            with open(os.path.join(target, "summary.csv"), newline="") as f:
                pairs = {r["key"]: r["value"] for r in csv.DictReader(f)}
            self.assertEqual(set(pairs), set(result["summary"]))
            for key, value in result["summary"].items():
                if isinstance(value, str):
                    self.assertEqual(pairs[key], value, key)
                else:
                    self.assertEqual(float(pairs[key]), value, key)

    def test_a_directory_that_cannot_be_made_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            blocker = os.path.join(directory, "a_file")
            with open(blocker, "w") as f:
                f.write("x")
            with self.assertRaises(Exception):
                abep.write_csv(os.path.join(blocker, "inside"), duration_days=0.01)


if __name__ == "__main__":
    unittest.main()
