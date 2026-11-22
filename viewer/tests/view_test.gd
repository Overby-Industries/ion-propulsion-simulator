extends SceneTree
## The scene of the orbit view, run headless: that it builds, plays the bundled sample, puts the satellite where the data says,
## shows the plume only while thrusting, turns the Earth at the model's rate, and handles bad files.
##   godot --headless --path viewer -s res://tests/view_test.gd

const Trajectory = preload("res://scripts/trajectory.gd")
const PlotStrip = preload("res://scripts/plot_strip.gd")

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
	_run()


func _run() -> void:
	await process_frame
	var view: Node3D = (load("res://scenes/orbit_view.tscn") as PackedScene).instantiate()
	root.add_child(view)
	view.playing = false          # before the first frame, which would otherwise move the time on
	var t: Trajectory = view.traj
	check(t.count > 100, "the scene loaded the bundled sample (%d samples)" % t.count)
	check(view.time_s == 0.0, "and starts at 0 s")
	await process_frame
	check(view.info_text().contains("ABEP orbit view") and view.info_text().contains("samples.csv"), "the info panel names the file")

	# The satellite is where the data says, in km scaled to the scene's units.
	view.set_time(0.0)
	check(view.satellite_position().distance_to(t.position_at(0.0) * view.KM) < 1.0e-4, "at 0 s the satellite is at the first sample")
	check(near(view.satellite_position().length(), (Trajectory.EARTH_RADIUS_KM + t.columns["altitude_km"][0]) * view.KM, 1.0e-4), "at the radius of the orbit: 6.628 units")
	var thrust_time := -1.0
	var coast_time := -1.0
	for i in t.count:
		if thrust_time < 0.0 and t.mode[i] == "THRUST" and t.columns["thrust_n"][i] > 0.0:
			thrust_time = t.time[i]
		if coast_time < 0.0 and t.mode[i] == "COAST":
			coast_time = t.time[i]
	check(thrust_time >= 0.0 and coast_time >= 0.0, "the sample has thrust and coast in it")
	view.set_time(thrust_time + 1.0)
	check(view.plume_visible() and view.info_text().contains("THRUST"), "in THRUST the plume shows and the panel says so")
	view.set_time(coast_time + 1.0)
	check(not view.plume_visible() and view.info_text().contains("COAST"), "in COAST there is no plume")
	var middle := t.duration() * 0.37
	view.set_time(middle)
	check(view.satellite_position().distance_to(t.position_at(middle) * view.KM) < 1.0e-4, "mid-run the satellite is where position_at says")
	check(view.info_text().contains("%.2f km" % t.value_at("altitude_km", middle)), "the panel shows the altitude of that moment")
	check(view.info_text().contains("thrust duty") and view.info_text().contains("decay with the thruster off"), "and the summary of the whole run")

	# The Earth turns at the model's rate, from the start; switched off it stays.
	view.set_time(5000.0)
	check(near(view.earth_angle(), view.EARTH_ROTATION * 5000.0, 1.0e-6), "the Earth has turned at 7.292115e-5 rad/s")
	view.set_earth_turns(false)
	view.set_time(9000.0)
	check(near(view.earth_angle(), 0.0, 1.0e-9) and not view._turns_box.button_pressed, "and does not when that is switched off (the box says so)")
	view.set_earth_turns(true)
	check(near(view.earth_angle(), view.EARTH_ROTATION * 9000.0, 1.0e-6), "and picks up the right angle when it is switched on again")

	# Time stays inside the run, and wraps when playing.
	view.set_time(-50.0)
	check(view.time_s == 0.0, "time cannot go before the start")
	view.set_time(t.duration() + 500.0)
	check(view.time_s == t.duration(), "or after the end")
	view.set_time(t.duration() + 500.0, true)
	check(near(view.time_s, 500.0, 1.0e-6), "unless it wraps round, as playing does")
	view.playing = true
	view.set_speed(600.0)
	var before: float = view.time_s
	view._process(1.0)
	check(near(view.time_s, before + 600.0, 1.0e-6), "playing at 600x moves 600 s in one second")
	view.toggle_play()
	check(not view.playing and view._play_button.text == "Play", "the play button pauses")
	view.set_speed(3600.0)
	check(view._speed_buttons[2].button_pressed and not view._speed_buttons[1].button_pressed, "and the speed buttons follow the speed")

	# The camera looks at its pivot from the distance asked for; follow mode pivots on the satellite.
	view.set_camera(0.5, 0.3, 30.0)
	check(near(view.camera_position().length(), 30.0, 1.0e-3), "the camera is 30 units from the Earth")
	var cam: Camera3D = view._camera
	check((-cam.global_transform.basis.z).dot((-view.camera_position()).normalized()) > 0.99999, "and looks at it")
	view.set_camera(0.5, 5.0, 0.0)
	check(view._pitch <= 1.5 and view._dist >= 0.06, "pitch and distance are held to sensible limits")
	view.set_time(12345.0)
	view.set_follow(true)
	check(view._pivot.distance_to(view.satellite_position()) < 1.0e-4 and view._dist <= 1.6, "follow pivots on the satellite and comes in close")
	check(view._follow_box.button_pressed, "and the Follow box shows it")
	view.set_time(23456.0)
	check(view._pivot.distance_to(view.satellite_position()) < 1.0e-4, "and stays with it")
	view.reset_view()
	check(not view.follow and view._pivot == Vector3.ZERO and near(view._dist, view.HOME_DIST, 1.0e-6), "reset view goes home")
	check(not view._follow_box.button_pressed, "and the Follow box is cleared with it")
	check(near(view._path_material.get_shader_parameter("half_width"), 0.03, 1.0e-9) and near(view._satellite.scale.x, 3.0, 1.0e-9), "at home the ribbons are 60 km wide and the satellite is drawn 3 times its size")
	view.set_camera(0.5, 0.3, 1.0)
	check(near(view._path_material.get_shader_parameter("half_width"), 0.004, 1.0e-9) and near(view._satellite.scale.x, 0.9, 1.0e-6), "close in the ribbons narrow to 8 km and the satellite comes down with the view")
	view.reset_view()

	# The drawn orbit follows the same arcs as the satellite, from the first sample to the last, at the orbit's radius. Its points
	# come in pairs on the orbit; the shader pulls a pair apart along the side vector, the standing ribbon's being the radius and
	# the lying one's being across the orbit.
	var ribbons: ArrayMesh = view._path.mesh
	var standing: Array = ribbons.surface_get_arrays(0)
	var lying: Array = ribbons.surface_get_arrays(1)
	var centres: PackedVector3Array = standing[Mesh.ARRAY_VERTEX]
	var sides: PackedVector3Array = standing[Mesh.ARRAY_NORMAL]
	var edges: PackedVector2Array = standing[Mesh.ARRAY_TEX_UV2]
	var flat_sides: PackedVector3Array = lying[Mesh.ARRAY_NORMAL]
	check(ribbons.get_surface_count() == 2 and centres.size() == 2 * (t.count - 1) * (view.PATH_STEPS + 1), "the path is two ribbons with %d points between samples" % view.PATH_STEPS)
	check(centres[0].distance_to(t.position[0] * view.KM) < 1.0e-5, "the path starts at the first sample")
	check(centres[centres.size() - 1].distance_to(t.position[t.count - 1] * view.KM) < 1.0e-5, "and ends at the last")
	var altitudes := t.range_of("altitude_km")
	var lowest := 1.0e9
	var highest := 0.0
	var pairs_ok := true
	var sides_ok := true
	for k in centres.size() / 2:
		var r := centres[2 * k].length()
		lowest = minf(lowest, r)
		highest = maxf(highest, r)
		pairs_ok = pairs_ok and centres[2 * k] == centres[2 * k + 1] and edges[2 * k].x == -1.0 and edges[2 * k + 1].x == 1.0
		var up := centres[2 * k].normalized()
		sides_ok = sides_ok and sides[2 * k].dot(up) > 0.995 and absf(flat_sides[2 * k].dot(up)) < 0.01 and absf(flat_sides[2 * k].length() - 1.0) < 0.01
	check(lowest > (Trajectory.EARTH_RADIUS_KM + altitudes.x - 30.0) * view.KM and highest < (Trajectory.EARTH_RADIUS_KM + altitudes.y + 30.0) * view.KM, "the path stays at the orbit's radius (the altitude is above the oblate Earth, so 30 km of slack)")
	check(pairs_ok, "every point is a pair on the orbit, one edge each way")
	check(sides_ok, "the standing ribbon is wide along the radius, the lying one across it")

	# The strip and the slider know the run.
	check(view._plot.traj == view.traj and view._slider.max_value == t.duration(), "the strip and the slider have the run")
	view.set_time(7000.0)
	check(view._slider.value == 7000.0 and view._plot.time_s == 7000.0, "and follow the time")
	check(near(view._plot.time_at_x(PlotStrip.LEFT), 0.0, 1.0e-9), "the left of the strip is 0 s")
	view._plot.size = Vector2(1000.0, 100.0)
	check(near(view._plot.time_at_x(1000.0 - PlotStrip.RIGHT), t.duration(), 1.0e-6), "and the right of it is the end")

	# Bad files are refused with the reason, and what was showing stays.
	check(not view.load_file("res://sample/missing.csv") and view.info_text().contains("Cannot open"), "a missing file is refused and the panel says so")
	check(view.traj == t, "and the old run is still the one showing")
	check(view.load_file("res://sample/samples.csv") and view.message == "" and view.time_s == 0.0, "a good file clears the message and starts from 0")
	check(view.shadow_visible(), "the shadow is shown by default")

	print("%d checks, %d failures" % [checks, failures])
	quit(1 if failures > 0 else 0)
