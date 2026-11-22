extends RefCounted
## One samples.csv written by abep_run (docs/OUTPUT.md), held as arrays, with the questions the view asks of it: which sample
## is at a time, where the satellite is then, in what mode. No scene and no rendering, so it is tested on its own
## (tests/trajectory_test.gd). Positions are kept in km, in the model's inertial frame, converted to the view's axes.

const EARTH_RADIUS_KM := 6378.137   ## the equatorial radius the model uses
const MODES := ["SAFE", "CHARGE", "COAST", "THRUST", "FAULT"]
const MODE_COLORS := {
	"SAFE": Color(0.95, 0.27, 0.27),
	"CHARGE": Color(0.96, 0.85, 0.27),
	"COAST": Color(0.36, 0.74, 0.95),
	"THRUST": Color(1.0, 0.55, 0.16),
	"FAULT": Color(0.85, 0.32, 0.95),
}
## What a file needs to be played: the columns of docs/OUTPUT.md that the view reads.
const REQUIRED := [
	"t_s", "mode", "altitude_km", "drag_n", "thrust_n", "net_drag_n", "thruster_power_w", "battery_wh", "temperature_k",
	"sunlit", "x_km", "y_km", "z_km",
]

var source := ""                         ## the file that was read
var error := ""                          ## why loading failed, if it did
var count := 0
var time := PackedFloat64Array()         ## s
var mode := PackedStringArray()
var sunlit := PackedByteArray()          ## 1 in sunlight, 0 in Earth's shadow
var position := PackedVector3Array()     ## km, in the view's axes (see inertial_to_view)
var columns := {}                        ## name -> PackedFloat64Array, for every column but the mode
var summary := {}                        ## key -> String, from the summary.csv beside the file, if there is one
var orbit_end := PackedFloat64Array()    ## the time each orbit ends at, from the orbits.csv beside the file, if there is one


## The model's frame has z along the Earth's axis; the view has y up. A rotation, not a mirror: the handedness is kept.
static func inertial_to_view(x: float, y: float, z: float) -> Vector3:
	return Vector3(x, z, -y)


func _clear() -> void:
	source = ""
	error = ""
	count = 0
	time = PackedFloat64Array()
	mode = PackedStringArray()
	sunlit = PackedByteArray()
	position = PackedVector3Array()
	columns = {}
	summary = {}
	orbit_end = PackedFloat64Array()


## Reads a samples.csv, and the summary.csv and orbits.csv in the same folder if they are there. Returns false, with `error`
## saying why, for a file that cannot be opened or is not a samples.csv this view can play.
func load_file(path: String) -> bool:
	_clear()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		error = "Cannot open %s (error %d)." % [path, FileAccess.get_open_error()]
		return false
	var header := file.get_csv_line()
	var index := {}
	for i in header.size():
		index[header[i].strip_edges()] = i
	for name in REQUIRED:
		if not index.has(name):
			error = "%s has no column %s: it is not a samples.csv this view can play (see docs/OUTPUT.md)." % [path.get_file(), name]
			return false
	# Plain arrays while reading: a packed array taken out of a dictionary is a copy, so appending to it would be lost.
	var raw := {}
	for name in index:
		if name != "mode":
			raw[name] = []
	while not file.eof_reached():
		var row := file.get_csv_line()
		if row.size() != header.size():
			continue   # the empty line at the end
		for name in raw:
			(raw[name] as Array).append(row[index[name]].to_float())
		time.append(row[index["t_s"]].to_float())
		mode.append(row[index["mode"]])
		sunlit.append(1 if row[index["sunlit"]].to_int() != 0 else 0)
		position.append(inertial_to_view(row[index["x_km"]].to_float(), row[index["y_km"]].to_float(), row[index["z_km"]].to_float()))
	for name in raw:
		columns[name] = PackedFloat64Array(raw[name])
	count = time.size()
	if count < 2:
		var reason := "%s has %d samples; the view needs at least two." % [path.get_file(), count]
		_clear()
		error = reason
		return false
	for i in range(1, count):
		if time[i] <= time[i - 1]:
			var reason := "%s: the time does not increase at row %d." % [path.get_file(), i + 2]
			_clear()
			error = reason
			return false
	source = path
	_load_summary(path.get_base_dir().path_join("summary.csv"))
	_load_orbits(path.get_base_dir().path_join("orbits.csv"))
	return true


func _load_summary(path: String) -> void:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return
	file.get_csv_line()   # key,value
	while not file.eof_reached():
		var row := file.get_csv_line()
		if row.size() == 2:
			summary[row[0]] = row[1]


func _load_orbits(path: String) -> void:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return
	var header := file.get_csv_line()
	var at := header.find("duration_s")
	if at < 0:
		return
	var total := 0.0
	while not file.eof_reached():
		var row := file.get_csv_line()
		if row.size() != header.size():
			continue
		total += row[at].to_float()
		orbit_end.append(total)


func duration() -> float:
	return time[count - 1] if count > 0 else 0.0


## The last sample at or before `t` (the first one if `t` is before it): the state in force at that time.
func index_at(t: float) -> int:
	if count == 0:
		return -1
	var lo := 0
	var hi := count - 1
	while lo < hi:
		var mid := (lo + hi + 1) >> 1
		if time[mid] <= t:
			lo = mid
		else:
			hi = mid - 1
	return lo


## How far `t` is from sample `i` to the next, 0 to 1.
func fraction_at(t: float, i: int) -> float:
	if i >= count - 1:
		return 0.0
	return clampf((t - time[i]) / (time[i + 1] - time[i]), 0.0, 1.0)


## Where the satellite is at `t`, km, in the view's axes: along the arc between two samples, not the chord, because the
## orbit is round.
func position_at(t: float) -> Vector3:
	var i := index_at(t)
	if i >= count - 1:
		return position[count - 1]
	return arc_point(i, fraction_at(t, i))


## The point `f` (0 to 1) of the way from sample `i` to the next, on that arc: what `position_at` follows, and what the drawn
## orbit is built from, so that the satellite stays on its line.
func arc_point(i: int, f: float) -> Vector3:
	var a := position[i]
	var b := position[mini(i + 1, count - 1)]
	var ra := a.length()
	var rb := b.length()
	if ra == 0.0 or rb == 0.0:
		return a.lerp(b, f)
	return (a / ra).slerp(b / rb, f) * lerpf(ra, rb, f)


## The direction of travel at `t`, a unit vector.
func heading_at(t: float) -> Vector3:
	var i := mini(index_at(t), count - 2)
	return (position[i + 1] - position[i]).normalized()


## A column between its samples, straight lines: for what changes smoothly (altitude, battery, temperature).
func value_at(column: String, t: float) -> float:
	var col: PackedFloat64Array = columns[column]
	var i := index_at(t)
	if i >= count - 1:
		return col[count - 1]
	return lerpf(col[i], col[i + 1], fraction_at(t, i))


## A column as the sample in force at `t` has it: for what a step does (the thrust, the drag, the power drawn).
func step_value_at(column: String, t: float) -> float:
	return (columns[column] as PackedFloat64Array)[index_at(t)]


func mode_at(t: float) -> String:
	return mode[index_at(t)]


func is_sunlit_at(t: float) -> bool:
	return sunlit[index_at(t)] != 0


## 1 for the orbit the time falls in, from orbits.csv; 0 if there is no orbits.csv.
func orbit_number_at(t: float) -> int:
	if orbit_end.is_empty():
		return 0
	for k in orbit_end.size():
		if t < orbit_end[k]:
			return k + 1
	return orbit_end.size()


func range_of(column: String) -> Vector2:
	var col: PackedFloat64Array = columns[column]
	var lo := col[0]
	var hi := col[0]
	for v in col:
		lo = minf(lo, v)
		hi = maxf(hi, v)
	return Vector2(lo, hi)


## The runs of one mode, as [{"from": s, "to": s, "mode": name}], in order, joined end to end from the first sample to the
## last. A run ends where the next one starts.
func segments() -> Array:
	var out: Array = []
	if count == 0:
		return out
	var start := 0
	for i in range(1, count + 1):
		if i == count or mode[i] != mode[start]:
			out.append({"from": time[start], "to": time[mini(i, count - 1)], "mode": mode[start]})
			start = i
	return out


## The runs in Earth's shadow, as [{"from": s, "to": s}].
func eclipses() -> Array:
	var out: Array = []
	var start := -1
	for i in count:
		if sunlit[i] == 0 and start < 0:
			start = i
		elif sunlit[i] != 0 and start >= 0:
			out.append({"from": time[start], "to": time[i]})
			start = -1
	if start >= 0:
		out.append({"from": time[start], "to": time[count - 1]})
	return out
