extends SceneTree
## The part of the orbit view that has no scene: reading a samples.csv and answering questions about it. Run headless:
##   godot --headless --path viewer -s res://tests/trajectory_test.gd

const Trajectory = preload("res://scripts/trajectory.gd")

var checks := 0
var failures := 0


func check(ok: bool, what: String) -> void:
	checks += 1
	if not ok:
		failures += 1
		print("  FAIL  ", what)


func near(a: float, b: float, tolerance: float) -> bool:
	return absf(a - b) <= tolerance


func _initialize() -> void:
	var t := Trajectory.new()
	check(t.load_file("res://sample/samples.csv"), "the bundled sample loads: " + t.error)
	print("  the bundled sample: %d samples over %.0f s" % [t.count, t.duration()])

	# One row per sample: count the data lines of the file by hand.
	var text := FileAccess.get_file_as_string("res://sample/samples.csv")
	var lines := 0
	for line in text.split("\n"):
		if line.strip_edges() != "":
			lines += 1
	check(t.count == lines - 1, "one sample per data line (%d of %d)" % [t.count, lines - 1])
	check(t.time[0] == 0.0, "the run starts at 0 s")
	var increasing := true
	for i in range(1, t.count):
		increasing = increasing and t.time[i] > t.time[i - 1]
	check(increasing, "the time increases")
	for name in Trajectory.REQUIRED:
		if name != "mode":
			check(t.columns.has(name) and (t.columns[name] as PackedFloat64Array).size() == t.count, "column %s has one value per sample" % name)

	# The positions are the orbit: at the start the radius is the equatorial radius plus the altitude; later the altitude is
	# measured above the ellipsoid, which lies inside the sphere of the equatorial radius by up to 21.4 km.
	var alt: PackedFloat64Array = t.columns["altitude_km"]
	check(near(t.position[0].length(), Trajectory.EARTH_RADIUS_KM + alt[0], 1.0e-2), "the first position is the equatorial radius plus the altitude")
	var on_orbit := true
	for i in t.count:
		var above := t.position[i].length() - Trajectory.EARTH_RADIUS_KM
		on_orbit = on_orbit and above <= alt[i] + 1.0e-2 and above >= alt[i] - 21.4 - 1.0e-2
	check(on_orbit, "every position agrees with its altitude, within the flattening of the Earth")

	# The frame: a rotation of the model's, so the handedness is kept.
	var ex := Trajectory.inertial_to_view(1, 0, 0)
	var ey := Trajectory.inertial_to_view(0, 1, 0)
	var ez := Trajectory.inertial_to_view(0, 0, 1)
	check(ex == Vector3(1, 0, 0) and ez == Vector3(0, 1, 0), "x stays x, and the Earth's axis z becomes up")
	check(ex.cross(ey).is_equal_approx(ez), "x cross y is z after the conversion too: right-handed")

	# Which sample is in force at a time.
	check(t.index_at(-5.0) == 0, "before the start, the first sample")
	check(t.index_at(t.time[3]) == 3, "at a sample, that sample")
	check(t.index_at((t.time[3] + t.time[4]) * 0.5) == 3, "between two, the earlier")
	check(t.index_at(t.duration() + 1000.0) == t.count - 1, "after the end, the last sample")
	check(near(t.fraction_at((t.time[3] + t.time[4]) * 0.5, 3), 0.5, 1.0e-12), "halfway is 0.5")

	# Between samples the satellite moves along the arc: at the middle the radius is the mean, and the angle half of the whole.
	var a := t.position[10]
	var b := t.position[11]
	var mid := t.position_at((t.time[10] + t.time[11]) * 0.5)
	check(near(mid.length(), (a.length() + b.length()) * 0.5, 1.0e-2), "the middle of an arc has the mean radius")
	check(near(a.angle_to(mid), a.angle_to(b) * 0.5, 1.0e-4), "and sits halfway round it")
	check(t.position_at(t.time[10]).distance_to(a) < 1.0e-2, "at a sample, exactly there")
	var chord_sag := a.length() * (1.0 - cos(a.angle_to(b) * 0.5))
	check(mid.length() > ((a + b) * 0.5).length() + chord_sag * 0.9, "the arc is outside the chord, which would cut the orbit short")
	check(t.heading_at(t.time[10]).dot((b - a).normalized()) > 0.999999, "the heading points to the next sample")
	# The drawn orbit is built from the same arcs, so the satellite is always on its line.
	# (A Vector3 holds 32-bit floats, so positions of 6600 km are good to about a metre: 1e-2 km is the tolerance.)
	check(t.arc_point(10, 0.0).distance_to(a) < 1.0e-2 and t.arc_point(10, 1.0).distance_to(b) < 1.0e-2, "an arc starts at its sample and ends at the next")
	check(t.arc_point(10, 0.5).distance_to(mid) < 1.0e-2, "arc_point is what position_at follows")
	var on_the_arc := true
	for step in 7:
		var f := step / 6.0
		on_the_arc = on_the_arc and t.arc_point(10, f).distance_to(t.position_at(lerpf(t.time[10], t.time[11], f))) < 1.0e-2
	check(on_the_arc, "at every sixth of the way too")
	check(t.arc_point(t.count - 1, 0.5).distance_to(t.position[t.count - 1]) < 1.0e-2, "and past the last sample it stays at the last")

	# Columns: smooth ones are interpolated, step ones are the sample in force.
	var battery: PackedFloat64Array = t.columns["battery_wh"]
	check(near(t.value_at("battery_wh", (t.time[20] + t.time[21]) * 0.5), (battery[20] + battery[21]) * 0.5, 1.0e-9), "the battery is interpolated")
	var thrust: PackedFloat64Array = t.columns["thrust_n"]
	check(t.step_value_at("thrust_n", (t.time[20] + t.time[21]) * 0.5) == thrust[20], "the thrust is the sample in force")
	check(t.mode_at(t.time[5]) == t.mode[5], "the mode is the sample's")
	check(t.is_sunlit_at(t.time[5]) == (t.sunlit[5] != 0), "sunlight is the sample's")

	# The runs of a mode cover the run end to end, and the run goes through several modes and several eclipses.
	var segments := t.segments()
	var joined: bool = segments.size() > 1 and segments[0]["from"] == 0.0 and segments[segments.size() - 1]["to"] == t.duration()
	for i in range(1, segments.size()):
		joined = joined and segments[i]["from"] == segments[i - 1]["to"] and segments[i]["mode"] != segments[i - 1]["mode"]
	check(joined, "the runs of a mode are joined end to end, from the first sample to the last, and differ from their neighbours")
	var seen := {}
	for seg in segments:
		seen[seg["mode"]] = true
		check(Trajectory.MODES.has(seg["mode"]), "%s is a known mode" % seg["mode"])
	check(seen.size() >= 3, "the sample goes through at least three modes (%s)" % ", ".join(seen.keys()))
	var eclipses := t.eclipses()
	check(eclipses.size() >= 10, "a day of this orbit has many eclipses (%d)" % eclipses.size())
	var eclipse_ok := true
	for e in eclipses:
		eclipse_ok = eclipse_ok and e["to"] > e["from"] and not t.is_sunlit_at((e["from"] + e["to"]) * 0.5)
	check(eclipse_ok, "every eclipse is a real span in shadow")
	var range_alt := t.range_of("altitude_km")
	check(range_alt.x <= alt[0] and range_alt.y >= alt[0], "the range of a column holds its first value")

	# summary.csv and orbits.csv beside the file.
	check(t.summary.has("thrust_duty") and float(t.summary["thrust_duty"]) > 0.0 and float(t.summary["thrust_duty"]) < 1.0, "summary.csv was read")
	check(t.summary.get("hold_status", "") == "Holds", "and the power to hold the altitude is there")
	check(t.orbit_end.size() >= 15, "orbits.csv was read: %d orbits" % t.orbit_end.size())
	# (The last sample can lie up to one sampling interval before the end of the run, which is what orbits.csv adds up to.)
	check(near(t.orbit_end[t.orbit_end.size() - 1], t.duration(), 1.5 * (t.time[1] - t.time[0])), "its last orbit ends where the run does")
	check(t.orbit_number_at(0.0) == 1 and t.orbit_number_at(t.duration()) == t.orbit_end.size(), "orbits are numbered from 1")

	# Files that cannot be played say why, and leave nothing half read.
	var missing := Trajectory.new()
	check(not missing.load_file("res://sample/nothing.csv") and missing.error.contains("Cannot open"), "a missing file is refused with a reason")
	var wrong := FileAccess.open("user://not_samples.csv", FileAccess.WRITE)
	wrong.store_string("a,b\n1,2\n")
	wrong.close()
	var bad := Trajectory.new()
	check(not bad.load_file("user://not_samples.csv") and bad.error.contains("t_s") and bad.count == 0, "a CSV without the columns is refused, naming one")
	var short := FileAccess.open("user://short.csv", FileAccess.WRITE)
	short.store_string(",".join(Trajectory.REQUIRED) + "\n")
	short.close()
	var tiny := Trajectory.new()
	check(not tiny.load_file("user://short.csv") and tiny.error.contains("at least two"), "a file with no samples is refused")
	var backwards := FileAccess.open("user://backwards.csv", FileAccess.WRITE)
	backwards.store_string(",".join(Trajectory.REQUIRED) + "\n")
	var order := Array(Trajectory.REQUIRED)
	for i in 2:
		var values := []
		for name in order:
			match name:
				"t_s": values.append("10" if i == 0 else "5")
				"mode": values.append("COAST")
				"sunlit": values.append("1")
				"x_km": values.append("6628")
				"battery_wh": values.append("500")
				_: values.append("0")
		backwards.store_string(",".join(values) + "\n")
	backwards.close()
	var reversed := Trajectory.new()
	check(not reversed.load_file("user://backwards.csv") and reversed.error.contains("does not increase"), "time that goes backwards is refused")
	for file_name in ["not_samples", "short", "backwards"]:
		DirAccess.remove_absolute("user://%s.csv" % file_name)   # what this test wrote

	print("%d checks, %d failures" % [checks, failures])
	quit(1 if failures > 0 else 0)
