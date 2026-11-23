extends SceneTree
## The Run panel of the orbit view, headless, against a real abep_run: that it reads abep_run's own defaults, turns the fields into
## abep_run's arguments and refuses what cannot be a setting, runs a mission and has the view play it, says what went wrong when
## abep_run refuses, stops a run that is going, and copes with there being no abep_run. ABEP_RUN has to name a built abep_run (the
## process is started for real, so the test takes some seconds, and it has no --quit-after: it ends when it is done):
##   ABEP_RUN=build/abep_run godot --headless --path viewer -s res://tests/run_panel_test.gd

const Trajectory = preload("res://scripts/trajectory.gd")
const RunPanel = preload("res://scripts/run_panel.gd")

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
	create_timer(240.0).timeout.connect(func() -> void:
		print("  FAIL  the test did not finish in 240 s")
		quit(2))
	var abep_run := OS.get_environment("ABEP_RUN")
	if abep_run == "" or not FileAccess.file_exists(abep_run):
		print("ABEP_RUN has to name a built abep_run (it is '%s')" % abep_run)
		quit(2)
		return

	# What abep_run's help says, as the panel reads it.
	var help := []
	OS.execute(abep_run, PackedStringArray(["--help"]), help, true)
	var table := RunPanel.parse_help(str(help[0]))
	check(table.size() >= 37, "the help lists the settings (%d)" % table.size())
	check(table[0]["key"] == "altitude_km" and table[0]["default"] == "250" and table[0]["meaning"].begins_with("circular orbit"), "the first is altitude_km = 250")
	check(RunPanel.parse_help("usage: x\n  a = 1   one\n    b = 2   not a setting line\n  c = 3.5   three\r\nnot a line\n").size() == 2, "only lines of the form '  name = default   meaning' are settings")
	check(RunPanel.find_abep_run() == abep_run, "find_abep_run gives the program that ABEP_RUN names")
	OS.set_environment("ABEP_RUN", "does/not/exist")
	check(RunPanel.find_abep_run() == "", "and none when ABEP_RUN names something that is not there")
	OS.set_environment("ABEP_RUN", abep_run)

	root.size = Vector2i(1600, 900)             # a headless window is 64 x 64
	await process_frame
	check(root.get_visible_rect().size == Vector2(1600, 900), "the test window is 1600 x 900: %s" % str(root.get_visible_rect().size))
	var view: Node3D = (load("res://scenes/orbit_view.tscn") as PackedScene).instantiate()
	root.add_child(view)
	view.playing = false
	await process_frame
	var panel: Window = view.run_panel()
	var folder := ProjectSettings.globalize_path("user://run_panel_test")      # the viewer's user folder has a space in its name
	panel.abep_run_path = abep_run
	panel.output_dir = folder
	var sample_count: int = view.traj.count
	check(sample_count > 100 and not panel.visible, "the view has its bundled sample, and the panel is closed")

	# The Run... button opens the panel, and again closes it; the fields start at abep_run's own defaults.
	view._run_open_button.pressed.emit()
	await process_frame
	check(panel.visible, "the Run... button opens the panel")
	check(panel.field_text("altitude_km") == "250" and panel.field_text("mass_kg") == "100" and panel.field_text("frontal_area_m2") == "1", "the fields start at abep_run's own defaults (250 km, 100 kg, 1 m^2)")
	check(panel.field_text("thrust_density_mn_per_kw") == "13.7" and panel.field_text("duration_days") == "1" and panel._atmosphere.get_selected_id() == 0, "and 13.7 mN/kW, 1 day, the 1976 atmosphere")
	check(panel.run_button_text() == "Run" and not panel._run_button.disabled, "the button says Run")
	view._run_open_button.pressed.emit()
	check(not panel.visible, "and the same button closes it")
	view._run_open_button.pressed.emit()
	check(panel.visible, "and opens it again")
	var closed_height := panel.size.y
	check(panel.position.y >= panel.TITLE_HEIGHT and panel.position.y + panel.size.y <= 900 and panel.position.x + panel.size.x <= 1600, "it opens inside the view, with its title bar showing: at %s, %s" % [panel.position, panel.size])
	panel._more_button.button_pressed = true
	check(panel._extra_box.visible and panel._more_button.text == "Fewer settings" and panel.size.y == closed_height + panel.MORE_HEIGHT, "More settings opens the box, and the window grows by its height: %d to %d px" % [closed_height, panel.size.y])
	check(panel.position.y >= panel.TITLE_HEIGHT and panel.position.y + panel.size.y <= 900, "and stays inside the view, title bar and all: at %s" % panel.position)
	panel._more_button.button_pressed = false
	check(not panel._extra_box.visible and panel.size.y == closed_height, "and closing it gives the room back")
	view._run_open_button.pressed.emit()

	# The fields become arguments: trimmed, in the panel's order, an empty one left to abep_run, then the lines of the box.
	panel.set_field("altitude_km", " 300 ")
	panel.set_field("mass_kg", "150")
	panel.set_field("frontal_area_m2", "0.5")
	panel.set_field("thrust_density_mn_per_kw", "")
	panel.set_atmosphere(2)
	panel.set_field("duration_days", "0.05")
	panel.set_extra_text("# a comment\n\n decay_max_years = 0.02   and a note\ninclination_deg=98\n")
	check(panel.arguments() == PackedStringArray(["altitude_km=300", "mass_kg=150", "frontal_area_m2=0.5", "atmosphere=2", "duration_days=0.05", "decay_max_years=0.02", "inclination_deg=98"]), "the fields and the box become abep_run's arguments: %s" % str(panel.arguments()))

	# What cannot be a setting is refused, with the reason, before anything is started.
	panel.set_field("mass_kg", "heavy")
	check(panel.arguments().is_empty() and panel.last_error.begins_with("Satellite mass, kg: 'heavy' is not a number"), "a field that is not a number is refused, by name: %s" % panel.last_error)
	panel.set_field("mass_kg", "1,5")
	check(panel.arguments().is_empty() and panel.last_error.contains("use a point for decimals"), "a decimal comma is refused with a hint")
	panel.set_field("mass_kg", "150")
	panel.set_extra_text("inclination_deg=98\nnovalue")
	check(panel.arguments().is_empty() and panel.last_error.begins_with("More settings, line 2: expected name=value"), "a line of the box without = is refused, by its number: %s" % panel.last_error)
	panel.set_extra_text("out=elsewhere")
	check(panel.arguments().is_empty() and panel.last_error.contains("out="), "out= is the panel's to choose")
	panel.set_extra_text("mass_kg=heavy")
	check(panel.arguments().is_empty() and panel.last_error.contains("'heavy' is not a number (setting 'mass_kg')"), "a value in the box that is not a number is refused")
	check(not panel.start_run() and not panel.is_running() and panel.status_text().contains("not a number"), "and Run does not start, and says why")

	# A run, for real: the process starts, the button says Stop, and when it ends the view plays what it wrote.
	panel.set_extra_text("decay_max_years=0.02")
	check(panel.start_run() and panel.is_running() and panel.run_button_text() == "Stop", "Run starts abep_run, and the button says Stop")
	check(not (panel._fields["altitude_km"] as LineEdit).editable and panel._atmosphere.disabled, "the fields are locked while it runs")
	check(not panel.start_run(), "a second run is not started on top of the first")
	var result: Array = await panel.finished
	check(result[0] == true and result[1] == folder.path_join("samples.csv"), "it finished well, with the samples.csv of the folder asked for: %s" % panel.status_text())
	check(not panel.is_running() and panel.run_button_text() == "Run" and (panel._fields["altitude_km"] as LineEdit).editable, "the button says Run again, and the fields are open")
	check(FileAccess.file_exists(folder.path_join("samples.csv")) and FileAccess.file_exists(folder.path_join("orbits.csv")) and FileAccess.file_exists(folder.path_join("summary.csv")), "abep_run wrote its three files in the folder (a path with a space in it)")
	var run: Trajectory = view.traj
	check(run.count == 72 and run.source == folder.path_join("samples.csv"), "the view plays the new run: %d samples from %s" % [run.count, run.source.get_file()])
	check(near(run.value_at("altitude_km", 0.0), 300.0, 0.001), "it starts at the 300 km that was asked for")
	check(view.playing and view.time_s == 0.0 and view.info_text().contains("the whole run") and view.info_text().contains("power to hold the altitude"), "from its start, playing, with its summary in the panel of numbers")
	check(panel.status_text().begins_with("Done.") and panel.status_text().contains("power to hold the altitude") and not panel.status_text().contains("files written to"), "the panel says Done, with abep_run's own summary lines")
	check(panel.files_button_visible() and OS.get_environment("ABEP_PAUSE") == "0", "Show the files appears, and abep_run was told never to wait for Enter")

	# abep_run refusing: its own words, and the view keeps what it was showing.
	panel.set_extra_text("nonsense=1")
	check(panel.start_run(), "a setting that does not exist gets as far as abep_run")
	result = await panel.finished
	check(result[0] == false and panel.status_text().begins_with("unknown setting 'nonsense'"), "abep_run refuses it, and the panel says so in abep_run's words: %s" % panel.status_text())
	check(view.traj == run and not panel.files_button_visible() and not panel.is_running(), "the view keeps the run it was showing, and the panel is ready again")
	panel.set_extra_text("atmosphere=9")
	check(panel.start_run(), "an atmosphere that does not exist gets as far as abep_run too")
	result = await panel.finished
	check(result[0] == false and panel.status_text().contains("atmosphere"), "and is refused by it: %s" % panel.status_text())

	# A run that takes a while can be stopped, and nothing is left running.
	panel.set_extra_text("")
	panel.set_field("altitude_km", "800")
	panel.set_field("duration_days", "1")
	check(panel.start_run(), "a long run (800 km: it is flown for ten years to see if it decays) starts")
	var pid: int = panel._pid
	for i in 15:
		await process_frame
	check(panel.is_running() and OS.is_process_running(pid), "and is still going after some frames")
	var pressed_at := Time.get_ticks_msec()
	panel._run_button.pressed.emit()
	result = await panel.finished
	var stopped_in := Time.get_ticks_msec() - pressed_at
	check(result[0] == false and panel.status_text() == "Stopped." and not panel.is_running() and not OS.is_process_running(pid), "the Stop button ends it: the panel says Stopped, and the process is gone")
	check(stopped_in < 2500, "and at once, not when the run would have ended by itself (that takes some 7 s): %d ms" % stopped_in)
	check(view.traj == run, "and the view keeps what it was showing")

	# Settings on the command line: the same run, with a folder of its own named by out=.
	var other := ProjectSettings.globalize_path("user://run_panel_test_cli")
	check(panel.start_run_with(PackedStringArray(["duration_days=0.05", "decay_max_years=0.02", "mass_kg=120", "out=" + other])), "start_run_with starts abep_run with arguments as they come from the command line")
	result = await panel.finished
	check(result[0] == true and result[1] == other.path_join("samples.csv") and view.traj.source == other.path_join("samples.csv") and view.traj.count == 72, "and the out= folder is where the view finds the run")

	# No abep_run at all.
	panel.abep_run_path = ""
	check(not panel.start_run() and panel.status_text().begins_with("abep_run was not found") and panel._run_button.disabled, "without an abep_run the panel says so, and cannot run")
	panel.abep_run_path = abep_run
	panel.start_run_with(PackedStringArray(["duration_days=0.05", "decay_max_years=0.02", "out=" + folder]))
	await panel.finished
	check(not panel._run_button.disabled and panel.field_text("altitude_km") == "800", "and with one again it can, and the fields were left as they were")

	for leftover in [folder, other]:             # the test's runs do not stay in the viewer's user folder
		for file in DirAccess.get_files_at(leftover):
			DirAccess.remove_absolute(leftover.path_join(file))
		DirAccess.remove_absolute(leftover)
	print("%d checks, %d failures" % [checks, failures])
	quit(1 if failures > 0 else 0)
