extends Node3D
## ABEP orbit view: plays back the CSV files that abep_run writes (docs/OUTPUT.md). A stylised Earth (made in code, not real
## geography), the orbit coloured by flight mode and dimmed in eclipse, the satellite with a plume while it thrusts, the
## Earth's cylindrical shadow (the model's own), and the numbers of the moment: mode, altitude, drag, thrust, battery.
##
##   godot --path viewer                           # plays the bundled sample, sample/samples.csv
##   godot --path viewer -- path/to/samples.csv    # or a file of yours, or a folder that holds one
##
## Left-drag orbits the camera, the wheel zooms, Space plays and pauses, the strip at the bottom shows the whole run and takes
## clicks. One scene unit is 1000 km; the Sun is fixed along +x, as in the model.

const Trajectory = preload("res://scripts/trajectory.gd")
const PlotStrip = preload("res://scripts/plot_strip.gd")
const PlanetTexture = preload("res://scripts/planet_texture.gd")

const KM := 0.001                       ## scene units per km
const EARTH_ROTATION := 7.292115e-5     ## rad/s, the rate the model's atmosphere turns at (constants.hpp)
const SAMPLE := "res://sample/samples.csv"
const SPEEDS := [60.0, 600.0, 3600.0, 21600.0]
const SPEED_NAMES := ["60x", "600x", "1 h/s", "6 h/s"]
const HOME_YAW := 0.95
const HOME_PITCH := 0.42
const HOME_DIST := 25.0
const PATH_STEPS := 6                   ## points along the drawn orbit between two samples (the samples are minutes apart)

const PATH_SHADER := """
shader_type spatial;
render_mode unshaded, cull_disabled;
uniform float now = 0.0;
uniform float half_width = 0.03;
void vertex() {
	// The vertices lie on the orbit: NORMAL is the way the ribbon is wide there, and UV2.x says which edge of it this one is.
	VERTEX += NORMAL * (UV2.x * half_width);
}
void fragment() {
	float past = step(UV.x, now);
	ALBEDO = COLOR.rgb * mix(0.55, 1.0, past);
	ALPHA = mix(0.75, 1.0, past);
}
"""

const GLOW_SHADER := """
shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never;
uniform vec3 glow : source_color = vec3(0.30, 0.55, 1.0);
uniform float power = 2.6;
uniform float strength = 1.3;
void fragment() {
	float f = pow(1.0 - clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0), power);
	ALBEDO = glow * f * strength;
	ALPHA = f;
}
"""

var traj := Trajectory.new()
var time_s := 0.0
var playing := true
var speed := 600.0                      ## simulated seconds per real second
var earth_turns := true
var follow := false
var message := ""                       ## shown in the info panel when something went wrong

var _yaw := HOME_YAW
var _pitch := HOME_PITCH
var _dist := HOME_DIST
var _home_yaw := HOME_YAW               ## the view "Reset view" returns to: worked out from the orbit, see _home_angles
var _home_pitch := HOME_PITCH
var _pivot := Vector3.ZERO
var _camera: Camera3D
var _earth: Node3D
var _shadow: MeshInstance3D
var _satellite: Node3D
var _plume: MeshInstance3D
var _path: MeshInstance3D
var _path_material: ShaderMaterial
var _info: RichTextLabel
var _plot: PlotStrip
var _slider: HSlider
var _play_button: Button
var _follow_box: CheckBox
var _turns_box: CheckBox
var _shadow_box: CheckBox
var _speed_buttons: Array[Button] = []
var _dialog: FileDialog
var _updating_slider := false


func _ready() -> void:
	_build_world()
	_build_ui()
	_apply_camera()
	# A file or a folder named after `--` is played; anything else on the command line (a test script's own arguments, say) is not
	# for this scene.
	var path := SAMPLE
	for arg in OS.get_cmdline_user_args():
		if arg.ends_with(".csv"):
			path = arg
			break
		if DirAccess.dir_exists_absolute(arg):
			path = arg.path_join("samples.csv")
			break
	load_file(path)


func _process(delta: float) -> void:
	if playing and traj.count > 1:
		set_time(time_s + delta * speed, true)


func _unhandled_input(event: InputEvent) -> void:
	var motion := event as InputEventMouseMotion
	var button := event as InputEventMouseButton
	var key := event as InputEventKey
	if motion != null and (motion.button_mask & (MOUSE_BUTTON_MASK_LEFT | MOUSE_BUTTON_MASK_RIGHT)) != 0:
		_yaw -= motion.relative.x * 0.006
		_pitch = clampf(_pitch + motion.relative.y * 0.006, -1.5, 1.5)
		_apply_camera()
	elif button != null and button.pressed:
		if button.button_index == MOUSE_BUTTON_WHEEL_UP:
			set_camera(_yaw, _pitch, _dist * 0.9)
		elif button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			set_camera(_yaw, _pitch, _dist / 0.9)
	elif key != null and key.pressed and not key.echo and key.keycode == KEY_SPACE:
		toggle_play()


# ---------------------------------------------------------------------------------------------------------------- the world

func _build_world() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.012, 0.016, 0.03)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.22, 0.27, 0.40)
	env.ambient_light_energy = 0.5
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.light_energy = 1.8
	add_child(sun)
	sun.look_at_from_position(Vector3(10, 0, 0), Vector3.ZERO, Vector3.UP)   # the light travels along -x

	_build_stars()
	_build_earth()
	_build_satellite()

	_camera = Camera3D.new()
	_camera.fov = 40.0
	_camera.near = 0.02
	_camera.far = 400.0
	add_child(_camera)
	_camera.make_current()

	var shader := Shader.new()
	shader.code = PATH_SHADER
	_path_material = ShaderMaterial.new()
	_path_material.shader = shader


func _build_stars() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 11
	var points := PackedVector3Array()
	var colors := PackedColorArray()
	for i in 700:
		var d := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized()
		points.append(d * 150.0)
		var b := rng.randf_range(0.25, 1.0)
		colors.append(Color(b, b, minf(1.0, b * 1.1), 1.0))
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = points
	arrays[Mesh.ARRAY_COLOR] = colors
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_POINTS, arrays)
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.vertex_color_use_as_albedo = true
	material.use_point_size = true
	material.point_size = 2.0
	var stars := MeshInstance3D.new()
	stars.mesh = mesh
	stars.material_override = material
	stars.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(stars)


func _build_earth() -> void:
	var radius := Trajectory.EARTH_RADIUS_KM * KM
	_earth = Node3D.new()
	add_child(_earth)
	var sphere := SphereMesh.new()
	sphere.radius = radius
	sphere.height = radius * 2.0
	sphere.radial_segments = 96
	sphere.rings = 48
	var surface := StandardMaterial3D.new()
	surface.albedo_texture = PlanetTexture.make(512, 256)
	surface.roughness = 0.92
	surface.metallic_specular = 0.25
	var globe := MeshInstance3D.new()
	globe.mesh = sphere
	globe.material_override = surface
	_earth.add_child(globe)

	# The thin glow of the atmosphere at the limb.
	var shell := SphereMesh.new()
	shell.radius = radius + 110.0 * KM
	shell.height = shell.radius * 2.0
	shell.radial_segments = 96
	shell.rings = 48
	var glow_shader := Shader.new()
	glow_shader.code = GLOW_SHADER
	var glow := ShaderMaterial.new()
	glow.shader = glow_shader
	var atmosphere := MeshInstance3D.new()
	atmosphere.mesh = shell
	atmosphere.material_override = glow
	atmosphere.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(atmosphere)

	# The cylinder of Earth's shadow behind it, as the model has it: no penumbra.
	var cylinder := CylinderMesh.new()
	cylinder.top_radius = radius
	cylinder.bottom_radius = radius
	cylinder.height = radius * 4.5
	cylinder.radial_segments = 64
	var dark := StandardMaterial3D.new()
	dark.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	dark.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	dark.albedo_color = Color(0.0, 0.0, 0.0, 0.34)
	dark.cull_mode = BaseMaterial3D.CULL_DISABLED
	_shadow = MeshInstance3D.new()
	_shadow.mesh = cylinder
	_shadow.material_override = dark
	_shadow.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_shadow.rotation = Vector3(0.0, 0.0, PI / 2.0)       # its axis along x
	_shadow.position = Vector3(-cylinder.height / 2.0, 0.0, 0.0)
	add_child(_shadow)

	# The Sun, a long way off along +x, for orientation.
	var sun_disc := MeshInstance3D.new()
	var disc := SphereMesh.new()
	disc.radius = 1.2
	disc.height = 2.4
	var glare := StandardMaterial3D.new()
	glare.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	glare.albedo_color = Color(1.0, 0.93, 0.62)
	sun_disc.mesh = disc
	sun_disc.material_override = glare
	sun_disc.position = Vector3(95.0, 0.0, 0.0)
	add_child(sun_disc)
	var label := Label3D.new()
	label.text = "Sun, fixed along +x"
	label.font_size = 48
	label.pixel_size = 0.04
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.modulate = Color(1.0, 0.93, 0.62)
	label.position = Vector3(95.0, 3.2, 0.0)
	add_child(label)


func _build_satellite() -> void:
	_satellite = Node3D.new()
	add_child(_satellite)
	var hull := StandardMaterial3D.new()
	hull.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	hull.albedo_color = Color(0.95, 0.97, 1.0)
	var wing := StandardMaterial3D.new()
	wing.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	wing.albedo_color = Color(0.25, 0.45, 0.95)
	var body := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.05, 0.035, 0.09)
	body.mesh = box
	body.material_override = hull
	_satellite.add_child(body)
	for side in [-1.0, 1.0]:
		var panel := MeshInstance3D.new()
		var plate := BoxMesh.new()
		plate.size = Vector3(0.11, 0.004, 0.06)
		panel.mesh = plate
		panel.material_override = wing
		panel.position = Vector3(side * 0.085, 0.0, 0.0)
		_satellite.add_child(panel)
	# The plume: shown while the thruster works. The satellite faces along its velocity, so behind is +z.
	var cone := CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 0.022
	cone.height = 0.16
	var fire := StandardMaterial3D.new()
	fire.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	fire.albedo_color = Color(1.0, 0.55, 0.16)
	_plume = MeshInstance3D.new()
	_plume.mesh = cone
	_plume.material_override = fire
	_plume.rotation = Vector3(PI / 2.0, 0.0, 0.0)        # apex backwards
	_plume.position = Vector3(0.0, 0.0, 0.045 + 0.08)
	_plume.visible = false
	_satellite.add_child(_plume)


## The orbit as two crossed ribbons along the samples (a line is one pixel wide and cannot be seen on a globe): one standing up
## from the surface, one lying in the orbit's plane, so that one of them faces the camera from any direction. Between two
## samples the ribbons follow the same arc as the satellite (`PATH_STEPS` points each), and have the colour of the mode in force
## there, a little darker in eclipse, with a sharp change where the mode changes. The time along the run goes in the UV, so that
## the shader can show the part already flown brighter than the part still to come. Each point is on the orbit, twice: the
## shader (PATH_SHADER) pulls the two apart along the normal, by the width `_apply_camera` sets, which narrows as the camera
## comes in, so that close to the satellite the orbit is a line beside it and not a wall around it.
func _build_path() -> void:
	if _path != null:
		_path.queue_free()
	var duration := traj.duration()
	var mesh := ArrayMesh.new()
	for ribbon in 2:
		var vertices := PackedVector3Array()
		var normals := PackedVector3Array()
		var edges := PackedVector2Array()
		var colors := PackedColorArray()
		var uvs := PackedVector2Array()
		for i in traj.count - 1:
			var c: Color = Trajectory.MODE_COLORS.get(traj.mode[i], Color(0.8, 0.8, 0.8))
			if traj.sunlit[i] == 0:
				c = Color(c.r * 0.62, c.g * 0.62, c.b * 0.62, 1.0)
			c = c.srgb_to_linear()   # the colours are as they look on screen; the shader's output is linear
			var ahead := (traj.position[i + 1] - traj.position[i]).normalized()
			for step in PATH_STEPS + 1:
				var f := step / float(PATH_STEPS)
				var p := traj.arc_point(i, f) * KM
				var up := p.normalized()
				var side := up if ribbon == 0 else up.cross(ahead).normalized()
				var uv := Vector2(lerpf(traj.time[i], traj.time[i + 1], f) / duration, 0.0)
				for edge in [-1.0, 1.0]:
					vertices.append(p)
					normals.append(side)
					edges.append(Vector2(edge, 0.0))
					colors.append(c)
					uvs.append(uv)
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = vertices
		arrays[Mesh.ARRAY_NORMAL] = normals
		arrays[Mesh.ARRAY_COLOR] = colors
		arrays[Mesh.ARRAY_TEX_UV] = uvs
		arrays[Mesh.ARRAY_TEX_UV2] = edges
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLE_STRIP, arrays)
	_path = MeshInstance3D.new()
	_path.mesh = mesh
	_path.material_override = _path_material
	_path.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_path)


# -------------------------------------------------------------------------------------------------------------------- the UI

func _panel_style() -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.03, 0.04, 0.08, 0.84)
	style.set_corner_radius_all(6)
	style.set_content_margin_all(10)
	return style


func _build_ui() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(root)

	# The numbers of the moment, top left.
	var info_panel := PanelContainer.new()
	info_panel.add_theme_stylebox_override("panel", _panel_style())
	info_panel.position = Vector2(12, 12)
	info_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(info_panel)
	_info = RichTextLabel.new()
	_info.bbcode_enabled = true
	_info.fit_content = true
	_info.scroll_active = false
	_info.autowrap_mode = TextServer.AUTOWRAP_OFF
	_info.custom_minimum_size = Vector2(340, 0)
	_info.mouse_filter = Control.MOUSE_FILTER_IGNORE
	info_panel.add_child(_info)

	# What the colours mean, top right.
	var legend_panel := PanelContainer.new()
	legend_panel.add_theme_stylebox_override("panel", _panel_style())
	legend_panel.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	legend_panel.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	legend_panel.offset_top = 12.0
	legend_panel.offset_right = -12.0
	legend_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(legend_panel)
	var legend := VBoxContainer.new()
	legend.mouse_filter = Control.MOUSE_FILTER_IGNORE
	legend_panel.add_child(legend)
	var legend_title := Label.new()
	legend_title.text = "flight mode"
	legend_title.add_theme_color_override("font_color", Color(0.55, 0.62, 0.76))
	legend.add_child(legend_title)
	for name in Trajectory.MODES:
		var row := HBoxContainer.new()
		row.mouse_filter = Control.MOUSE_FILTER_IGNORE
		var swatch := ColorRect.new()
		swatch.color = Trajectory.MODE_COLORS[name]
		swatch.custom_minimum_size = Vector2(14, 14)
		swatch.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		swatch.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(swatch)
		var text := Label.new()
		text.text = " " + name
		row.add_child(text)
		legend.add_child(row)
	var note := Label.new()
	note.text = "half as bright: eclipse"
	note.add_theme_color_override("font_color", Color(0.55, 0.62, 0.76))
	legend.add_child(note)

	# The strip and the controls, along the bottom.
	var bottom := VBoxContainer.new()
	bottom.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	bottom.grow_vertical = Control.GROW_DIRECTION_BEGIN
	bottom.offset_left = 12.0
	bottom.offset_right = -12.0
	bottom.offset_bottom = -12.0
	bottom.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(bottom)
	_plot = PlotStrip.new()
	_plot.custom_minimum_size = Vector2(0, 118)
	_plot.scrubbed.connect(_on_scrubbed)
	bottom.add_child(_plot)

	var bar_panel := PanelContainer.new()
	bar_panel.add_theme_stylebox_override("panel", _panel_style())
	bottom.add_child(bar_panel)
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 8)
	bar_panel.add_child(bar)
	var open_button := Button.new()
	open_button.text = "Open..."
	open_button.pressed.connect(_on_open_pressed)
	bar.add_child(open_button)
	_play_button = Button.new()
	_play_button.custom_minimum_size = Vector2(76, 0)
	_play_button.pressed.connect(toggle_play)
	bar.add_child(_play_button)
	var group := ButtonGroup.new()
	for i in SPEEDS.size():
		var b := Button.new()
		b.text = SPEED_NAMES[i]
		b.toggle_mode = true
		b.button_group = group
		b.pressed.connect(set_speed.bind(SPEEDS[i]))
		bar.add_child(b)
		_speed_buttons.append(b)
	_slider = HSlider.new()
	_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_slider.custom_minimum_size = Vector2(200, 0)
	_slider.min_value = 0.0
	_slider.max_value = 1.0
	_slider.step = 1.0
	_slider.value_changed.connect(_on_slider_changed)
	bar.add_child(_slider)
	_follow_box = CheckBox.new()
	_follow_box.text = "Follow"
	_follow_box.toggled.connect(set_follow)
	bar.add_child(_follow_box)
	_turns_box = CheckBox.new()
	_turns_box.text = "Earth turns"
	_turns_box.toggled.connect(set_earth_turns)
	bar.add_child(_turns_box)
	_shadow_box = CheckBox.new()
	_shadow_box.text = "Shadow"
	_shadow_box.toggled.connect(func(on: bool) -> void: _shadow.visible = on)
	bar.add_child(_shadow_box)
	var home_button := Button.new()
	home_button.text = "Reset view"
	home_button.pressed.connect(reset_view)
	bar.add_child(home_button)
	var help := Label.new()
	help.text = "drag: orbit   wheel: zoom   space: play"
	help.add_theme_color_override("font_color", Color(0.55, 0.62, 0.76))
	bar.add_child(help)

	_dialog = FileDialog.new()
	_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	_dialog.filters = PackedStringArray(["*.csv ; CSV files written by abep_run"])
	_dialog.title = "Open the samples.csv of a run"
	_dialog.size = Vector2i(820, 520)
	_dialog.file_selected.connect(load_file)
	add_child(_dialog)

	_sync_buttons()


# ----------------------------------------------------------------------------------------------------------------- controls

## Reads a samples.csv (and the summary.csv and orbits.csv beside it) and shows it from the start. Returns false, and says why
## in the info panel, for a file that cannot be played; what was showing stays.
func load_file(path: String) -> bool:
	var fresh := Trajectory.new()
	if not fresh.load_file(path):
		message = fresh.error
		_info.text = info_text()
		return false
	message = ""
	traj = fresh
	_build_path()
	_plot.set_trajectory(traj)
	_slider.max_value = traj.duration()
	var home := _home_angles()
	_home_yaw = home.x
	_home_pitch = home.y
	reset_view()
	set_time(0.0)
	return true


## A camera direction that shows this orbit well: 63 degrees off the orbit's axis, so that the orbit is a tilted ring and not a
## line or a circle on the Earth's edge; 55 degrees round that axis from the Sun's side, so that the lit side, the terminator and
## the shadow behind are all in view; and from the north. Returns (yaw, pitch). Without an orbit, a fixed view.
func _home_angles() -> Vector2:
	if traj.count < 3:
		return Vector2(HOME_YAW, HOME_PITCH)
	var axis := traj.position[0].cross(traj.position[1]).normalized()
	var sun := Vector3(1.0, 0.0, 0.0)
	var towards_sun := sun - axis * sun.dot(axis)
	towards_sun = towards_sun.normalized() if towards_sun.length() > 1.0e-6 else Vector3(0.0, 0.0, 1.0)
	var across := axis.cross(towards_sun)
	var best := Vector3.ZERO
	var best_height := -2.0
	for turn in [0.96, -0.96]:
		for tilt in [0.45, -0.45]:
			var d: Vector3 = (axis * tilt + (towards_sun * cos(turn) + across * sin(turn)) * sqrt(1.0 - tilt * tilt)).normalized()
			if d.y > best_height:
				best_height = d.y
				best = d
	return Vector2(atan2(best.x, best.z), asin(clampf(best.y, -1.0, 1.0)))


## Goes to a time in the run (seconds from its start). With `wrap`, a time past the end starts again from the beginning.
func set_time(t: float, wrap := false) -> void:
	if traj.count < 2:
		return
	var d := traj.duration()
	if wrap and t > d:
		t = fmod(t, d)
	time_s = clampf(t, 0.0, d)
	_update_world()


func toggle_play() -> void:
	playing = not playing
	_sync_buttons()


func set_speed(value: float) -> void:
	speed = value
	_sync_buttons()


func set_follow(on: bool) -> void:
	follow = on
	if on:
		_dist = minf(_dist, 1.6)
		_pivot = traj.position_at(time_s) * KM if traj.count > 1 else Vector3.ZERO
	else:
		_pivot = Vector3.ZERO
		_dist = maxf(_dist, HOME_DIST * 0.6)
	_apply_camera()
	_update_world()
	_sync_buttons()


func set_earth_turns(on: bool) -> void:
	earth_turns = on
	_earth.rotation.y = EARTH_ROTATION * time_s if on else 0.0
	_sync_buttons()


func set_camera(yaw: float, pitch: float, dist: float) -> void:
	_yaw = yaw
	_pitch = clampf(pitch, -1.5, 1.5)
	_dist = clampf(dist, 0.06, 90.0)
	_apply_camera()


func reset_view() -> void:
	follow = false
	_pivot = Vector3.ZERO
	set_camera(_home_yaw, _home_pitch, HOME_DIST)
	_update_world()
	_sync_buttons()


func camera_position() -> Vector3:
	return _camera.global_position


func satellite_position() -> Vector3:
	return _satellite.position


func plume_visible() -> bool:
	return _plume.visible


func shadow_visible() -> bool:
	return _shadow.visible


func earth_angle() -> float:
	return _earth.rotation.y


func _apply_camera() -> void:
	var offset := Vector3(cos(_pitch) * sin(_yaw), sin(_pitch), cos(_pitch) * cos(_yaw)) * _dist
	_camera.look_at_from_position(_pivot + offset, _pivot, Vector3.UP)
	_camera.v_offset = -_dist * 0.05   # what is looked at sits a little above the middle of the window, clear of the strip below
	# The orbit's ribbons are 60 km wide seen from afar and narrow down to 8 km as the camera comes in; the satellite, which is
	# only a marker, grows with the view up to 3 times its drawn size and stays the same size on the screen when it is close.
	_path_material.set_shader_parameter("half_width", clampf(_dist * 0.0016, 0.004, 0.03))
	if _satellite != null:
		_satellite.scale = Vector3.ONE * clampf(_dist * 0.9, 0.02, 3.0)


func _sync_buttons() -> void:
	if _play_button == null:
		return
	_play_button.text = "Pause" if playing else "Play"
	for i in _speed_buttons.size():
		_speed_buttons[i].set_pressed_no_signal(is_equal_approx(speed, SPEEDS[i]))
	_follow_box.set_pressed_no_signal(follow)
	_turns_box.set_pressed_no_signal(earth_turns)
	_shadow_box.set_pressed_no_signal(_shadow.visible)


func _on_open_pressed() -> void:
	_dialog.popup_centered()


func _on_scrubbed(t: float) -> void:
	set_time(t)


func _on_slider_changed(value: float) -> void:
	if not _updating_slider:
		set_time(value)


# ------------------------------------------------------------------------------------------------------------------ the moment

func _update_world() -> void:
	if traj.count < 2:
		_info.text = info_text()
		return
	var position := traj.position_at(time_s) * KM
	var heading := traj.heading_at(time_s)
	_satellite.look_at_from_position(position, position + heading, position.normalized())
	_plume.visible = traj.mode_at(time_s) == "THRUST" and traj.step_value_at("thrust_n", time_s) > 0.0
	if earth_turns:
		_earth.rotation.y = EARTH_ROTATION * time_s
	_path_material.set_shader_parameter("now", time_s / traj.duration())
	if follow:
		_pivot = position
		_apply_camera()
	_info.text = info_text()
	_plot.time_s = time_s
	_updating_slider = true
	_slider.value = time_s
	_updating_slider = false


func _clock(t: float) -> String:
	var total := int(t)
	return "%d:%02d:%02d" % [total / 3600, (total / 60) % 60, total % 60]


## The text of the info panel: the moment shown, and what the summary.csv says about the whole run.
func info_text() -> String:
	var lines := PackedStringArray()
	lines.append("[b]ABEP orbit view[/b]")
	if message != "":
		lines.append("[color=#ff7a66]%s[/color]" % message)
	if traj.count < 2:
		lines.append("Open a samples.csv written by abep_run.")
		return "\n".join(lines)
	lines.append("[color=#8d9bb5]%s[/color]" % traj.source.get_file())
	var m := traj.mode_at(time_s)
	var color: Color = Trajectory.MODE_COLORS.get(m, Color.WHITE)
	var orbit := traj.orbit_number_at(time_s)
	lines.append("time  %s%s" % [_clock(time_s), "   orbit %d" % orbit if orbit > 0 else ""])
	lines.append("mode  [color=#%s][b]%s[/b][/color]   %s" % [color.to_html(false), m, "sunlight" if traj.is_sunlit_at(time_s) else "Earth's shadow"])
	lines.append("altitude  %.2f km" % traj.value_at("altitude_km", time_s))
	var drag := traj.step_value_at("drag_n", time_s)
	var thrust := traj.step_value_at("thrust_n", time_s)
	lines.append("drag  %.2f mN" % (drag * 1000.0))
	lines.append("thrust  %.2f mN    net drag  %+.2f mN" % [thrust * 1000.0, (drag - thrust) * 1000.0])
	lines.append("thruster power  %.0f W" % traj.step_value_at("thruster_power_w", time_s))
	lines.append("battery  %.1f Wh" % traj.value_at("battery_wh", time_s))
	lines.append("thruster temperature  %.1f K" % traj.value_at("temperature_k", time_s))
	var s := traj.summary
	if s.has("thrust_duty"):
		lines.append("")
		lines.append("[color=#8d9bb5]the whole run (summary.csv)[/color]")
		lines.append("thrust duty  %.3f" % float(s["thrust_duty"]))
		if s.has("decay_time_days"):
			lines.append("decay with the thruster off  %.1f days" % float(s["decay_time_days"]))
		if s.get("hold_status", "") == "Holds":
			lines.append("power to hold the altitude  %.0f W (array %.0f W)" % [float(s["hold_power_w"]), float(s["hold_array_power_w"])])
		elif s.has("hold_status"):
			lines.append("power to hold the altitude: the intake cannot supply the drag")
	return "\n".join(lines)
