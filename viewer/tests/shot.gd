extends SceneTree
## A frame of the orbit view with the real renderer, for looking at it (and for the README picture):
##
##   godot --path viewer --resolution 1600x900 -s res://tests/shot.gd -- <name> [thrust|coast|eclipse] [yaw pitch dist] [follow] [panel...]
##
## Saves viewer/shots/<name>.png (that folder is git-ignored); a name ending in .jpg is saved as a small JPEG. The second argument
## picks a moment that is in that state: the first one after two hours with the satellite on the side of the Earth the camera sees
## and clear of the panels (`thrust` is the default). `follow` has the camera follow the satellite, as the checkbox does: from
## alongside it, unless a yaw, pitch and distance are given. `panel` opens the Run panel after a run from it (300 km, 150 kg; ABEP_RUN
## has to name an abep_run); `panel-error` shows abep_run refusing a setting, `panel-running` a run that is going, and `panel-more` the
## box of other settings with the list of them.

const Trajectory = preload("res://scripts/trajectory.gd")

func _initialize() -> void:
	_run()


func _run() -> void:
	await process_frame
	var args := Array(OS.get_cmdline_user_args())
	var follow := args.has("follow")
	args.erase("follow")
	var panel_mode := ""
	for a in args.duplicate():
		if str(a).begins_with("panel"):
			panel_mode = a
			args.erase(a)
	var shot_name: String = args[0] if args.size() > 0 else "orbit_view"
	var want: String = args[1] if args.size() > 1 else "thrust"
	var view: Node3D = (load("res://scenes/orbit_view.tscn") as PackedScene).instantiate()
	root.add_child(view)
	await process_frame
	view.playing = false
	if panel_mode != "":
		var panel: Window = view.run_panel()
		if OS.get_environment("ABEP_RUN") != "":
			panel.abep_run_path = OS.get_environment("ABEP_RUN")
		panel.open()
		panel.set_field("altitude_km", "300")
		panel.set_field("mass_kg", "150")
		match panel_mode:
			"panel-more":      # the box of other settings open, and the list of all of them
				panel._more_button.button_pressed = true
				panel.set_extra_text("inclination_deg=98\nthruster_power_w=500")
				panel._show_settings_list()
			"panel-error":     # abep_run refusing a setting
				panel.set_extra_text("nonsense=1")
				panel.start_run()
				await panel.finished
			"panel-running":   # a run that takes a while, caught while it is going
				panel.set_field("altitude_km", "800")
				panel.start_run()
				await create_timer(4.5).timeout
			_:                 # a finished run
				panel.set_extra_text("decay_max_years=0.02")
				panel.start_run()
				await panel.finished
				view.playing = false
	var t: Trajectory = view.traj
	var camera: Camera3D = view._camera
	var screen: Vector2 = root.get_visible_rect().size
	var clear := Rect2(screen * Vector2(0.3, 0.12), screen * Vector2(0.4, 0.5))   # the middle, away from the panels and the strip
	var toward_camera: Vector3 = camera.global_position.normalized()
	var moment := 0.0
	for i in t.count:
		var p: Vector3 = t.position[i] * view.KM
		if t.time[i] < 7200.0 or p.normalized().dot(toward_camera) < 0.45 or not clear.has_point(camera.unproject_position(p)):
			continue
		var match_found := false
		match want:
			"thrust": match_found = t.mode[i] == "THRUST" and t.columns["thrust_n"][i] > 0.0
			"coast": match_found = t.mode[i] == "COAST"
			"eclipse": match_found = t.sunlit[i] == 0
			_: match_found = true
		if match_found:
			moment = t.time[i]
			break
	view.set_time(moment)
	if follow:
		view.set_follow(true)
	if args.size() > 4:
		view.set_camera(float(args[2]), float(args[3]), float(args[4]))
	elif follow:
		# Alongside the satellite, from out of the orbit's plane and a little above it, close enough to see the plume.
		var radial: Vector3 = view.satellite_position().normalized()
		var side: Vector3 = t.heading_at(moment).cross(radial).normalized()
		var d: Vector3 = (side * cos(0.5) + radial * sin(0.5)).normalized()
		view.set_camera(atan2(d.x, d.z), asin(d.y), 0.7)
	for i in 6:
		await process_frame
	await RenderingServer.frame_post_draw
	var image: Image = root.get_viewport().get_texture().get_image()
	var jpg := shot_name.ends_with(".jpg")
	var path := ProjectSettings.globalize_path("res://shots/%s" % (shot_name if jpg else shot_name + ".png"))
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	print("saved ", path, " at t = ", moment, " s, err=", image.save_jpg(path, 0.88) if jpg else image.save_png(path))
	print("the satellite is at ", camera.unproject_position(view.satellite_position()), " of ", screen)
	quit(0)
