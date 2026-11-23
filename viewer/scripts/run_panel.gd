extends Window
## The Run panel of the orbit view: fields for the settings that people change first (the altitude, the mass, the frontal area, the
## thrust per power, the atmosphere, the length of the run), a box for any other setting, and a Run button that starts abep_run with
## them. abep_run is a process of its own, so the view keeps working while it runs, and the button turns into Stop. When it is
## done the panel says what abep_run said and emits `finished`, and the view plays the result.
##
## abep_run is the program that the environment variable ABEP_RUN names, or abep_run(.exe) next to the viewer (as in the Windows
## download), or, when the viewer runs from the source tree, the one in ../build. A run's files go to the viewer's user folder,
## user://last_run, where every run replaces the last one, unless the run is asked for with an out=FOLDER of its own. The settings
## and their defaults are read from `abep_run --help`, so they cannot differ from abep_run's own.

signal finished(ok: bool, samples_path: String)

const ORDER := ["altitude_km", "mass_kg", "frontal_area_m2", "thrust_density_mn_per_kw", "atmosphere", "duration_days"]
const LABELS := {
	"altitude_km": "Orbit altitude, km",
	"mass_kg": "Satellite mass, kg",
	"frontal_area_m2": "Frontal area, m²",
	"thrust_density_mn_per_kw": "Thrust per power, mN/kW",
	"atmosphere": "Atmosphere",
	"duration_days": "Length of the run, days",
}
const ATMOSPHERES := ["1976 standard", "ECSS low activity", "ECSS moderate activity", "ECSS high, long term", "ECSS high, short term"]
const RUN_FOLDER := "user://last_run"
const MIN_SIZE := Vector2i(380, 560)    ## the smallest the window can be with the box of other settings closed
const MORE_HEIGHT := 200                ## what the box of other settings adds to the window's height (a label's wrapped height is not
										## known before it has a width, so this is a number and not a measurement)
const TITLE_HEIGHT := 40                ## the title bar sits above the position of an embedded window: keep it on the screen
const ERROR_COLOR := "#ff7a66"
const OK_COLOR := "#7fd88c"
const NOTE_COLOR := "#8d9bb5"

## The program that runs a mission; empty if there is none. Setting it makes the panel read abep_run's settings again.
var abep_run_path := "":
	set(value):
		abep_run_path = value
		_prepared = false
var output_dir := ""                  ## where a run's files go, unless the run names its own out=
var settings: Array = []              ## what `abep_run --help` lists: dictionaries with key, default and meaning
var samples_path := ""                ## the samples.csv of the run that is going, or of the last one
var last_error := ""                  ## why `arguments()` gave nothing

var _fields := {}                     ## setting name -> LineEdit
var _atmosphere: OptionButton
var _extra: TextEdit
var _extra_box: VBoxContainer
var _more_button: Button
var _run_button: Button
var _files_button: Button
var _status: RichTextLabel
var _prepared := false
var _placed := false
var _pid := -1
var _stdio: FileAccess
var _stderr: FileAccess
var _out_bytes := PackedByteArray()
var _err_bytes := PackedByteArray()
var _started_ms := 0
var _shown_seconds := -1
var _stopping := false


func _init() -> void:
	title = "Run a mission"
	visible = false                   # before it is in the tree, or it would open at once
	exclusive = false
	min_size = MIN_SIZE
	size = Vector2i(410, 640)
	close_requested.connect(hide)
	_build()


func _ready() -> void:
	if abep_run_path == "":
		abep_run_path = find_abep_run()
	if output_dir == "":
		output_dir = ProjectSettings.globalize_path(RUN_FOLDER)


func _exit_tree() -> void:
	if _pid >= 0:
		OS.kill(_pid)                 # a process made with execute_with_pipe does not end with the viewer


# --------------------------------------------------------------------------------------------------------- finding abep_run

## The abep_run to use: the one ABEP_RUN names, else the one next to the viewer's exe, else (from the source tree) one in ../build.
## An empty string if there is none.
static func find_abep_run() -> String:
	var program := "abep_run.exe" if OS.has_feature("windows") else "abep_run"
	var named := OS.get_environment("ABEP_RUN")
	if named != "":
		return named if FileAccess.file_exists(named) else ""
	var project := ProjectSettings.globalize_path("res://")
	for candidate in [
		OS.get_executable_path().get_base_dir().path_join(program),
		project.path_join("../build").path_join(program),
		project.path_join("../build/Release").path_join(program),
	]:
		if FileAccess.file_exists(candidate):
			return String(candidate).simplify_path()
	return ""


## The settings that `abep_run --help` lists, one to a line as "  name = default   meaning": an array of dictionaries with the keys
## key, default and meaning, in the order of the help.
static func parse_help(text: String) -> Array:
	var pattern := RegEx.new()
	pattern.compile("^  (\\w+) = (\\S+)\\s+(.*)$")
	var found := []
	for line in text.split("\n"):
		var m := pattern.search(line.rstrip("\r"))
		if m != null:
			found.append({"key": m.get_string(1), "default": m.get_string(2), "meaning": m.get_string(3)})
	return found


## Reads abep_run's settings (once for each abep_run) and puts the defaults in the fields that have nothing in them.
func _prepare() -> void:
	if _prepared:
		return
	_prepared = true
	settings = []
	if abep_run_path == "":
		_run_button.disabled = true
		_say("[color=%s]%s[/color]" % [ERROR_COLOR, _not_found_text()])
		return
	_run_button.disabled = false
	var output := []
	if OS.execute(abep_run_path, PackedStringArray(["--help"]), output, true) == 0 and not output.is_empty():
		settings = parse_help(str(output[0]))
	for key in _fields:
		var edit: LineEdit = _fields[key]
		edit.placeholder_text = _default_of(key)
		if edit.text == "":
			edit.text = _default_of(key)
	var atmosphere := _default_of("atmosphere")
	if atmosphere.is_valid_int():
		_atmosphere.select(clampi(int(atmosphere), 0, ATMOSPHERES.size() - 1))


func _default_of(key: String) -> String:
	for s in settings:
		if s["key"] == key:
			return s["default"]
	return ""


func _not_found_text() -> String:
	return "abep_run was not found. It belongs next to the viewer (abep_run.exe, in the Windows download), or the environment variable ABEP_RUN can name it."


# ------------------------------------------------------------------------------------------------------------------ the panel

func _build() -> void:
	var background := PanelContainer.new()
	background.set_anchors_preset(Control.PRESET_FULL_RECT)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.06, 0.1)
	style.set_content_margin_all(14)
	background.add_theme_stylebox_override("panel", style)
	add_child(background)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	background.add_child(column)

	var intro := Label.new()
	intro.text = "Runs abep_run with these numbers, and plays the result in the view. Enter in a field runs it too."
	intro.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	intro.add_theme_color_override("font_color", Color(NOTE_COLOR))
	column.add_child(intro)

	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 12)
	grid.add_theme_constant_override("v_separation", 8)
	column.add_child(grid)
	for key in ORDER:
		var label := Label.new()
		label.text = LABELS[key]
		grid.add_child(label)
		if key == "atmosphere":
			_atmosphere = OptionButton.new()
			for i in ATMOSPHERES.size():
				_atmosphere.add_item(ATMOSPHERES[i], i)
			_atmosphere.fit_to_longest_item = false
			_atmosphere.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			grid.add_child(_atmosphere)
		else:
			var edit := LineEdit.new()
			edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			edit.custom_minimum_size = Vector2(120, 0)
			edit.select_all_on_focus = true
			edit.text_submitted.connect(_on_field_submitted)
			_style_input(edit)
			grid.add_child(edit)
			_fields[key] = edit

	_more_button = Button.new()
	_more_button.text = "More settings..."
	_more_button.toggle_mode = true
	_more_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	_more_button.toggled.connect(_on_more_toggled)
	column.add_child(_more_button)

	_extra_box = VBoxContainer.new()
	_extra_box.visible = false
	_extra_box.add_theme_constant_override("separation", 6)
	column.add_child(_extra_box)
	var hint := Label.new()
	hint.text = "Any other setting, one to a line, as name=value (the later of two lines for a setting wins):"
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.add_theme_color_override("font_color", Color(NOTE_COLOR))
	_extra_box.add_child(hint)
	_extra = TextEdit.new()
	_extra.custom_minimum_size = Vector2(0, 84)
	_extra.placeholder_text = "inclination_deg=98\nthruster_power_w=500"
	_style_input(_extra)
	_extra_box.add_child(_extra)
	var list_button := Button.new()
	list_button.text = "List all the settings"
	list_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	list_button.pressed.connect(_show_settings_list)
	_extra_box.add_child(list_button)

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	column.add_child(row)
	_run_button = Button.new()
	_run_button.text = "Run"
	_run_button.custom_minimum_size = Vector2(120, 36)
	_run_button.pressed.connect(_on_run_pressed)
	row.add_child(_run_button)
	_files_button = Button.new()
	_files_button.text = "Show the files"
	_files_button.visible = false
	_files_button.pressed.connect(_on_files_pressed)
	row.add_child(_files_button)

	_status = RichTextLabel.new()
	_status.bbcode_enabled = true
	_status.selection_enabled = true
	_status.scroll_active = true
	_status.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_status.custom_minimum_size = Vector2(0, 110)
	var status_style := StyleBoxFlat.new()
	status_style.bg_color = Color(0.025, 0.03, 0.055)
	status_style.set_corner_radius_all(4)
	status_style.set_content_margin_all(8)
	_status.add_theme_stylebox_override("normal", status_style)
	column.add_child(_status)


## A text field with an outline you can see on the dark panel, a brighter one when it has the cursor, and a dim one when it is locked.
func _style_input(field: Control) -> void:
	var normal := _input_style(Color(0.1, 0.12, 0.18), Color(0.27, 0.33, 0.46))
	field.add_theme_stylebox_override("normal", normal)
	field.add_theme_stylebox_override("focus", _input_style(Color(0.1, 0.12, 0.18), Color(0.45, 0.65, 1.0)))
	field.add_theme_stylebox_override("read_only", _input_style(Color(0.07, 0.08, 0.12), Color(0.16, 0.19, 0.27)))


func _input_style(fill: Color, outline: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = fill
	style.border_color = outline
	style.set_border_width_all(1)
	style.set_corner_radius_all(3)
	style.content_margin_left = 7.0
	style.content_margin_right = 7.0
	style.content_margin_top = 4.0
	style.content_margin_bottom = 4.0
	return style


## Shows the panel, where it was left (the first time near the top right of the view), with the cursor in the first field.
func open() -> void:
	_prepare()
	if _placed:
		show()
	else:
		_placed = true
		var area := _view_area()
		popup(Rect2i(Vector2i(maxi(8, area.x - size.x - 16), clampi(52, TITLE_HEIGHT, maxi(TITLE_HEIGHT, area.y - size.y))), size))
	grab_focus()
	(_fields["altitude_km"] as LineEdit).grab_focus()


func toggle() -> void:
	if visible:
		hide()
	else:
		open()


## The box of other settings opens and closes, and the window grows and shrinks by its height (so that the status box keeps its
## room), staying inside the view.
func _on_more_toggled(on: bool) -> void:
	_extra_box.visible = on
	_more_button.text = "Fewer settings" if on else "More settings..."
	var view_height := _view_area().y
	var wanted := size.y + (MORE_HEIGHT if on else -MORE_HEIGHT)      # before min_size, which moves size at once
	min_size = Vector2i(MIN_SIZE.x, mini(MIN_SIZE.y + (MORE_HEIGHT if on else 0), view_height))
	size = Vector2i(size.x, mini(maxi(min_size.y, wanted), view_height))
	position.y = clampi(position.y, TITLE_HEIGHT, maxi(TITLE_HEIGHT, view_height - size.y))


## The size of the view the window is in.
func _view_area() -> Vector2i:
	var parent := get_parent()
	if parent != null and parent.get_viewport() != null:
		return Vector2i(parent.get_viewport().get_visible_rect().size)
	return Vector2i(1152, 648)


func _on_field_submitted(_text: String) -> void:
	if not is_running():
		start_run()


func _on_run_pressed() -> void:
	if is_running():
		stop_run()
	else:
		start_run()


func _on_files_pressed() -> void:
	OS.shell_show_in_file_manager(samples_path.get_base_dir())


## Puts all the settings that abep_run has, with their defaults and meanings, in the box below.
func _show_settings_list() -> void:
	_prepare()
	var lines := PackedStringArray()
	for s in settings:
		lines.append("[b]%s[/b] = %s   [color=%s]%s[/color]" % [s["key"], _escape(s["default"]), NOTE_COLOR, _escape(s["meaning"])])
	_say("\n".join(lines) if not lines.is_empty() else "[color=%s]There is no list: abep_run was not found.[/color]" % ERROR_COLOR)


func _say(bbcode: String) -> void:
	_status.text = bbcode


func _escape(text: String) -> String:
	return text.replace("[", "[lb]")


## What the status box says, without its formatting.
func status_text() -> String:
	return _status.get_parsed_text()


func field_text(key: String) -> String:
	return (_fields[key] as LineEdit).text if _fields.has(key) else ""


func set_field(key: String, text: String) -> void:
	(_fields[key] as LineEdit).text = text


func set_atmosphere(index: int) -> void:
	_atmosphere.select(index)


func set_extra_text(text: String) -> void:
	_extra.text = text


func run_button_text() -> String:
	return _run_button.text


func files_button_visible() -> bool:
	return _files_button.visible


# ------------------------------------------------------------------------------------------------------------------ the run

## The arguments for abep_run that the fields stand for, in the order of the panel: a field left empty is left to abep_run's own
## default, and the lines of the box of other settings follow, to override. An empty array, and in `last_error` the reason, when
## something cannot be a setting.
func arguments() -> PackedStringArray:
	last_error = ""
	var args := PackedStringArray()
	for key in ORDER:
		if key == "atmosphere":
			args.append("atmosphere=%d" % _atmosphere.get_selected_id())
			continue
		var text := (_fields[key] as LineEdit).text.strip_edges()
		if text == "":
			continue
		if not text.is_valid_float():
			last_error = "%s: '%s' is not a number%s." % [LABELS[key], text, " (use a point for decimals)" if text.contains(",") else ""]
			return PackedStringArray()
		args.append("%s=%s" % [key, text])
	var equals := RegEx.new()
	equals.compile("\\s*=\\s*")
	var number := 0
	for raw in _extra.text.split("\n"):
		number += 1
		var line := raw.strip_edges().replace("\t", " ")
		if line == "" or line.begins_with("#"):
			continue
		var word := equals.sub(line, "=").get_slice(" ", 0)      # a note may follow the number, as in settings.txt
		var eq := word.find("=")
		if eq <= 0:
			last_error = "More settings, line %d: expected name=value, got '%s'." % [number, word]
			return PackedStringArray()
		var setting := word.substr(0, eq)
		var value := word.substr(eq + 1)
		if setting == "out":
			last_error = "More settings, line %d: the panel chooses the folder (out=); the files are in the folder that Show the files opens." % number
			return PackedStringArray()
		if not value.is_valid_float():
			last_error = "More settings, line %d: '%s' is not a number (setting '%s')." % [number, value, setting]
			return PackedStringArray()
		args.append(word)
	return args


## Starts abep_run with what the panel says. False, with the reason in the status box, if it did not start.
func start_run() -> bool:
	_prepare()
	if is_running():
		return false
	var args := arguments()
	if args.is_empty():
		_fail_to_start(last_error if last_error != "" else _not_found_text())
		return false
	return start_run_with(args)


## Starts abep_run with these arguments (name=value texts, and an out=FOLDER if the files are to go somewhere else than the
## panel's folder). This is what Run does, and what the viewer does for settings on its command line.
func start_run_with(given: PackedStringArray) -> bool:
	_prepare()
	if is_running():
		return false
	if abep_run_path == "":
		_fail_to_start(_not_found_text())
		return false
	var args := given.duplicate()                   # packed arrays are passed by reference
	var folder := output_dir
	var named_folder := false
	for arg in args:
		if arg.begins_with("out="):
			folder = arg.substr(4)
			named_folder = true
	if not named_folder:
		args.append("out=" + output_dir)
	OS.set_environment("ABEP_PAUSE", "0")           # abep_run must never wait for Enter here
	var started := OS.execute_with_pipe(abep_run_path, args, false)
	if started.is_empty():
		_fail_to_start("abep_run would not start: %s" % abep_run_path)
		return false
	_pid = started["pid"]
	_stdio = started["stdio"]
	_stderr = started["stderr"]
	_out_bytes.clear()
	_err_bytes.clear()
	_stopping = false
	_started_ms = Time.get_ticks_msec()
	_shown_seconds = -1
	samples_path = folder.path_join("samples.csv")
	_files_button.visible = false
	_set_busy(true)
	_say("Running...")
	return true


func is_running() -> bool:
	return _pid >= 0


## Ends the run that is going. The panel then says it was stopped.
func stop_run() -> void:
	if _pid >= 0 and not _stopping:
		_stopping = true
		OS.kill(_pid)


func _set_busy(busy: bool) -> void:
	_run_button.text = "Stop" if busy else "Run"
	for key in _fields:
		(_fields[key] as LineEdit).editable = not busy
	_atmosphere.disabled = busy
	_extra.editable = not busy


func _fail_to_start(why: String) -> void:
	_say("[color=%s]%s[/color]" % [ERROR_COLOR, _escape(why)])
	print("ABEP orbit view: run not started: ", why)


func _process(_delta: float) -> void:
	if _pid < 0:
		return
	_drain()
	if OS.is_process_running(_pid):
		var seconds := (Time.get_ticks_msec() - _started_ms) / 1000
		if seconds != _shown_seconds:
			_shown_seconds = seconds
			var more := "\n[color=%s]A high orbit takes several seconds: with the thruster off it is flown for up to ten years, to see whether it decays.[/color]" % NOTE_COLOR if seconds >= 3 else ""
			_say("Running... %d s%s" % [seconds, more])
		return
	_drain()
	var code := OS.get_process_exit_code(_pid)
	_pid = -1
	_stdio = null
	_stderr = null
	_finish(code)


## Takes what the process has written so far. The pipes are not blocking: with nothing to read, get_buffer gives nothing.
func _drain() -> void:
	for pipe in [_stdio, _stderr]:
		if pipe == null:
			continue
		var chunk: PackedByteArray = pipe.get_buffer(65536)
		while chunk.size() > 0:
			if pipe == _stdio:
				_out_bytes.append_array(chunk)
			else:
				_err_bytes.append_array(chunk)
			chunk = pipe.get_buffer(65536)


func _finish(code: int) -> void:
	var said := _out_bytes.get_string_from_utf8()
	var complained := _err_bytes.get_string_from_utf8()
	_set_busy(false)
	if _stopping:
		_stopping = false
		_say("[color=%s]Stopped.[/color]" % NOTE_COLOR)
		print("ABEP orbit view: run stopped")
		finished.emit(false, "")
		return
	if code != 0:
		var why := complained.strip_edges().get_slice("\n", 0).strip_edges()
		if why.begins_with("abep_run: "):
			why = why.substr(10)
		_fail(why if why != "" else "abep_run stopped with exit code %d." % code)
		return
	if not FileAccess.file_exists(samples_path):
		_fail("abep_run finished, but there is no samples.csv in %s." % samples_path.get_base_dir())
		return
	var lines := PackedStringArray()
	for line in said.split("\n"):
		var text := line.strip_edges()
		if text != "" and not text.begins_with("files written to"):
			lines.append(_escape(text))
	_say("[color=%s][b]Done.[/b][/color]  The run is playing in the view.\n%s" % [OK_COLOR, "\n".join(lines)])
	_files_button.visible = true
	finished.emit(true, samples_path)


func _fail(why: String) -> void:
	_say("[color=%s]%s[/color]" % [ERROR_COLOR, _escape(why)])
	print("ABEP orbit view: run failed: ", why)
	finished.emit(false, "")
