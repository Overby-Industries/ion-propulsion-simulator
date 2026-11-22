extends RefCounted
## A stylised planet surface made in code: nothing is downloaded, and nothing here is real geography (the model has no epoch
## and no map, so the continents mean nothing). Continents and oceans come from noise on the sphere, there is polar ice, and a
## graticule every 30 degrees, broader and brighter at longitude 0, so that the turning of the Earth can be seen. Longitude runs east
## with the texture's x, as Godot's SphereMesh lays it out: x = 0.25 is longitude 0, towards +x in the scene.


static func make(width: int = 512, height: int = 256, seed_value: int = 7) -> ImageTexture:
	var noise := FastNoiseLite.new()
	noise.seed = seed_value
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.fractal_type = FastNoiseLite.FRACTAL_FBM
	noise.fractal_octaves = 5
	noise.frequency = 1.0
	var image := Image.create_empty(width, height, false, Image.FORMAT_RGB8)
	for y in height:
		var lat := PI * (0.5 - (y + 0.5) / float(height))
		var lat_deg := rad_to_deg(lat)
		var lat_line := absf(fposmod(lat_deg + 15.0, 30.0) - 15.0) < 0.5
		for x in width:
			var lon := TAU * ((x + 0.5) / float(width) - 0.25)
			var p := Vector3(cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon))
			var n := noise.get_noise_3d(p.x * 1.6, p.y * 1.6, p.z * 1.6)
			var color: Color
			if absf(lat_deg) > 72.0 + 8.0 * n:
				color = Color(0.92, 0.95, 0.98)
			elif n > 0.07:
				color = Color(0.16, 0.38, 0.17).lerp(Color(0.52, 0.42, 0.27), clampf((n - 0.07) / 0.4, 0.0, 1.0))
			else:
				color = Color(0.10, 0.30, 0.50).lerp(Color(0.03, 0.10, 0.27), clampf((0.07 - n) / 0.5, 0.0, 1.0))
			var lon_deg := rad_to_deg(lon)
			if absf(lon_deg) < 0.8:
				color = color.lerp(Color.WHITE, 0.6)   # the prime meridian: broader and brighter than the rest (red would read as the SAFE mode)
			elif lat_line or absf(fposmod(lon_deg + 15.0, 30.0) - 15.0) < 0.5:
				color = color.lerp(Color.WHITE, 0.28)
			image.set_pixel(x, y, color)
	return ImageTexture.create_from_image(image)
