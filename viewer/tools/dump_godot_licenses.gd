extends SceneTree
## Writes the licence of the Godot Engine, and the notices of the components inside it, to a text file. A game exported with Godot
## carries the engine, and the engine's licence asks for its notice to travel with the copies; the release script puts this file
## in the download. Run with the project's own Godot:
##   godot --headless --path viewer -s res://tools/dump_godot_licenses.gd -- path/to/Godot.txt


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		printerr("usage: godot --headless --path viewer -s res://tools/dump_godot_licenses.gd -- <output file>")
		quit(2)
		return
	var rule := "=".repeat(78)
	var text := PackedStringArray()
	var version: Dictionary = Engine.get_version_info()
	text.append("Godot Engine %s" % version["string"])
	text.append("https://godotengine.org")
	text.append("")
	text.append(Engine.get_license_text())
	text.append("")
	text.append(rule)
	text.append("Components inside the engine, with their copyright and licence")
	text.append(rule)
	var components: Array = Engine.get_copyright_info()
	for component: Dictionary in components:
		text.append("")
		text.append(str(component["name"]))
		for part: Dictionary in component["parts"]:
			text.append("  Files: " + ", ".join(part["files"]))
			for line: String in part["copyright"]:
				text.append("  Copyright: " + line)
			text.append("  License: " + str(part["license"]))
	text.append("")
	text.append(rule)
	text.append("The texts of those licences")
	text.append(rule)
	var licences: Dictionary = Engine.get_license_info()
	for license_name: String in licences:
		text.append("")
		text.append("---- %s ----" % license_name)
		text.append(str(licences[license_name]))
	DirAccess.make_dir_recursive_absolute(args[0].get_base_dir())
	var file := FileAccess.open(args[0], FileAccess.WRITE)
	if file == null:
		printerr("cannot write %s (error %d)" % [args[0], FileAccess.get_open_error()])
		quit(1)
		return
	file.store_string("\n".join(text) + "\n")
	file.close()
	print("wrote %s: %d components, %d licence texts" % [args[0], components.size(), licences.size()])
	quit(0)
