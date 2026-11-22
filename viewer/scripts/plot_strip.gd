extends Control
## The whole run at a glance, under the 3D view: altitude, battery, the flight modes and the eclipses, with a cursor at the time
## being shown. Click or drag on it to go to a time.

signal scrubbed(time_s: float)

const Trajectory = preload("res://scripts/trajectory.gd")

const LEFT := 92.0    ## room for the row names
const RIGHT := 10.0

var traj: Trajectory
var time_s := 0.0:
	set(value):
		time_s = value
		queue_redraw()


func set_trajectory(t: Trajectory) -> void:
	traj = t
	queue_redraw()


func time_at_x(x: float) -> float:
	var w := size.x - LEFT - RIGHT
	return clampf((x - LEFT) / w, 0.0, 1.0) * traj.duration()


func _gui_input(event: InputEvent) -> void:
	if traj == null or traj.count < 2:
		return
	var button := event as InputEventMouseButton
	var motion := event as InputEventMouseMotion
	if (button != null and button.pressed and button.button_index == MOUSE_BUTTON_LEFT) \
			or (motion != null and (motion.button_mask & MOUSE_BUTTON_MASK_LEFT) != 0):
		scrubbed.emit(time_at_x((event as InputEventMouse).position.x))
		accept_event()


func _draw() -> void:
	var font := ThemeDB.fallback_font
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.03, 0.04, 0.08, 0.86))
	if traj == null or traj.count < 2:
		draw_string(font, Vector2(12, 26), "Nothing to show: open a samples.csv written by abep_run.", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color(0.7, 0.75, 0.85))
		return
	var w := size.x - LEFT - RIGHT
	var duration := traj.duration()
	var row_h := (size.y - 8.0) / 4.0
	_series("altitude_km", "altitude", "km", 0, w, row_h, Color(0.55, 0.85, 1.0))
	_series("battery_wh", "battery", "Wh", 1, w, row_h, Color(0.60, 0.95, 0.55))
	# The modes, and under them the eclipses.
	var modes_top := 4.0 + 2.0 * row_h + 3.0
	draw_string(font, Vector2(6, modes_top + 14), "mode", HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(0.85, 0.9, 1.0))
	for seg in traj.segments():
		var color: Color = Trajectory.MODE_COLORS.get(seg["mode"], Color(0.6, 0.6, 0.6))
		var x0: float = LEFT + w * float(seg["from"]) / duration
		var x1: float = LEFT + w * float(seg["to"]) / duration
		draw_rect(Rect2(x0, modes_top, maxf(1.0, x1 - x0), row_h - 8.0), color)
	var eclipse_top := 4.0 + 3.0 * row_h + 3.0
	draw_string(font, Vector2(6, eclipse_top + 13), "eclipse", HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(0.85, 0.9, 1.0))
	draw_rect(Rect2(LEFT, eclipse_top, w, row_h - 8.0), Color(0.10, 0.11, 0.16))
	for e in traj.eclipses():
		var x0: float = LEFT + w * float(e["from"]) / duration
		var x1: float = LEFT + w * float(e["to"]) / duration
		draw_rect(Rect2(x0, eclipse_top, maxf(1.0, x1 - x0), row_h - 8.0), Color(0.62, 0.64, 0.78))
	# The cursor.
	var cx := LEFT + w * clampf(time_s / duration, 0.0, 1.0)
	draw_line(Vector2(cx, 0.0), Vector2(cx, size.y), Color(1, 1, 1, 0.9), 1.5)


func _series(column: String, name: String, unit: String, row: int, w: float, row_h: float, color: Color) -> void:
	var font := ThemeDB.fallback_font
	var top := 4.0 + row * row_h + 3.0
	var h := row_h - 8.0
	var duration := traj.duration()
	var r := traj.range_of(column)
	var span := maxf(r.y - r.x, 1e-9)
	var col: PackedFloat64Array = traj.columns[column]
	var stride := maxi(1, int(traj.count / w))
	var points := PackedVector2Array()
	for i in range(0, traj.count, stride):
		points.append(Vector2(LEFT + w * traj.time[i] / duration, top + h * (1.0 - (col[i] - r.x) / span)))
	if points.size() >= 2:
		draw_polyline(points, color, 1.6, true)
	draw_string(font, Vector2(6, top + 12), name, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(0.85, 0.9, 1.0))
	draw_string(font, Vector2(6, top + 25), "%s to %s %s" % [_short(r.x), _short(r.y), unit], HORIZONTAL_ALIGNMENT_LEFT, LEFT - 8.0, 10, Color(0.6, 0.66, 0.78))


## A number for a label: three or four figures, whatever its size. (GDScript's % has no %g.)
static func _short(x: float) -> String:
	var a := absf(x)
	if a >= 100.0:
		return "%.0f" % x
	if a >= 10.0:
		return "%.1f" % x
	return "%.2f" % x
