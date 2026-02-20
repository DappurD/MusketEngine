extends CanvasLayer

## Pheromone Debug HUD — 2D panel with minimap heat map + channel info
##
## Shows a corner minimap of the selected pheromone channel as a color-coded
## heat map, plus channel selector, peak value, team indicator, and controls.
## Toggle with F6, switch channels with , / . (comma/period), team with T.

var pheromone_map: PheromoneMapCPP = null
var sim: SimulationServer = null  # For team switching
var active_channel: int = 0
var active_team: int = 0
var hud_opacity: float = 0.85

# Layout
const PANEL_SIZE := Vector2(280, 340)
const MAP_SIZE := Vector2(256, 170)  # Matches ~150x100 grid aspect ratio
const MARGIN := 8
const FONT_SIZE := 13
const FONT_SIZE_SMALL := 11
const FONT_SIZE_TITLE := 15

# Internal state
var _panel: PanelContainer
var _map_texture_rect: TextureRect
var _image: Image
var _texture: ImageTexture
var _title_label: Label
var _channel_label: Label
var _stats_label: Label
var _team_label: Label
var _controls_label: Label
var _channel_indicator: ColorRect  # Color swatch
var _update_timer: float = 0.0
var _display_max: float = 1.0
const UPDATE_INTERVAL := 0.1

# Channel metadata (shared with overlay)
const CHANNEL_COLORS := {
	0: Color(1.0, 0.0, 0.0),    1: Color(1.0, 0.5, 0.0),
	2: Color(1.0, 1.0, 0.0),    3: Color(0.0, 1.0, 0.0),
	4: Color(0.0, 0.5, 1.0),    5: Color(0.5, 0.0, 1.0),
	6: Color(1.0, 0.0, 1.0),    7: Color(0.0, 1.0, 1.0),
	8: Color(0.7, 0.7, 0.7),    9: Color(0.4, 0.8, 1.0),
	10: Color(1.0, 1.0, 0.3),   11: Color(0.8, 0.3, 0.3),
	12: Color(1.0, 0.6, 0.0),   13: Color(0.3, 0.8, 0.3),
	14: Color(0.5, 0.5, 0.5),
}

const CHANNEL_NAMES := {
	0: "DANGER", 1: "SUPPRESSION", 2: "CONTACT", 3: "RALLY",
	4: "FEAR", 5: "COURAGE", 6: "SAFE_ROUTE", 7: "FLANK_OPP",
	8: "METAL", 9: "CRYSTAL", 10: "ENERGY", 11: "CONGESTION",
	12: "BUILD_URG", 13: "EXPLORED", 14: "SPARE",
}

const CHANNEL_GROUPS := {
	"Combat": [0, 1, 2, 3, 4, 5, 6, 7],
	"Economy": [8, 9, 10, 11, 12, 13, 14],
}


func _ready():
	layer = 101  # Above other HUD elements
	_build_ui()


func _build_ui():
	# Dark semi-transparent panel in bottom-right corner
	_panel = PanelContainer.new()
	_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE

	var style = StyleBoxFlat.new()
	style.bg_color = Color(0.08, 0.08, 0.12, hud_opacity)
	style.border_color = Color(0.3, 0.3, 0.4, 0.8)
	style.set_border_width_all(1)
	style.set_corner_radius_all(4)
	style.set_content_margin_all(MARGIN)
	_panel.add_theme_stylebox_override("panel", style)

	var vbox = VBoxContainer.new()
	vbox.mouse_filter = Control.MOUSE_FILTER_IGNORE
	vbox.add_theme_constant_override("separation", 4)
	_panel.add_child(vbox)

	# Title row: "PHEROMONES" + team badge
	var title_row = HBoxContainer.new()
	title_row.mouse_filter = Control.MOUSE_FILTER_IGNORE

	_title_label = Label.new()
	_title_label.text = "PHEROMONES"
	_title_label.add_theme_font_size_override("font_size", FONT_SIZE_TITLE)
	_title_label.add_theme_color_override("font_color", Color(0.9, 0.9, 0.95))
	_title_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title_row.add_child(_title_label)

	_team_label = Label.new()
	_team_label.add_theme_font_size_override("font_size", FONT_SIZE)
	_team_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	title_row.add_child(_team_label)
	_update_team_label()

	vbox.add_child(title_row)

	# Channel indicator row: color swatch + channel name
	var ch_row = HBoxContainer.new()
	ch_row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	ch_row.add_theme_constant_override("separation", 6)

	_channel_indicator = ColorRect.new()
	_channel_indicator.custom_minimum_size = Vector2(14, 14)
	_channel_indicator.mouse_filter = Control.MOUSE_FILTER_IGNORE
	ch_row.add_child(_channel_indicator)

	_channel_label = Label.new()
	_channel_label.add_theme_font_size_override("font_size", FONT_SIZE)
	_channel_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	ch_row.add_child(_channel_label)

	vbox.add_child(ch_row)
	_update_channel_label()

	# Separator
	var sep1 = HSeparator.new()
	sep1.mouse_filter = Control.MOUSE_FILTER_IGNORE
	sep1.add_theme_color_override("separator", Color(0.3, 0.3, 0.4, 0.6))
	vbox.add_child(sep1)

	# Heat map TextureRect
	_map_texture_rect = TextureRect.new()
	_map_texture_rect.custom_minimum_size = MAP_SIZE
	_map_texture_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_map_texture_rect.expand_mode = TextureRect.EXPAND_FIT_WIDTH
	_map_texture_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	vbox.add_child(_map_texture_rect)

	# Stats row below map
	_stats_label = Label.new()
	_stats_label.add_theme_font_size_override("font_size", FONT_SIZE_SMALL)
	_stats_label.add_theme_color_override("font_color", Color(0.7, 0.8, 0.7, 0.9))
	_stats_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	vbox.add_child(_stats_label)

	# Separator
	var sep2 = HSeparator.new()
	sep2.mouse_filter = Control.MOUSE_FILTER_IGNORE
	sep2.add_theme_color_override("separator", Color(0.3, 0.3, 0.4, 0.6))
	vbox.add_child(sep2)

	# Controls hint
	_controls_label = Label.new()
	_controls_label.add_theme_font_size_override("font_size", FONT_SIZE_SMALL)
	_controls_label.add_theme_color_override("font_color", Color(0.5, 0.5, 0.6, 0.8))
	_controls_label.text = ",/. Channel  T:Team  F6:Close"
	_controls_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	vbox.add_child(_controls_label)

	add_child(_panel)

	# Initialize heat map image if pheromone_map already set
	if pheromone_map:
		_init_heat_map()


func _init_heat_map():
	if not pheromone_map:
		return
	var w = pheromone_map.get_width()
	var h = pheromone_map.get_height()
	_image = Image.create(w, h, false, Image.FORMAT_RGBA8)
	_image.fill(Color(0, 0, 0, 0))
	_texture = ImageTexture.create_from_image(_image)
	_map_texture_rect.texture = _texture
	_display_max = 1.0


func _process(delta):
	if not pheromone_map:
		return

	# Lazy init
	if not _image:
		_init_heat_map()

	# Position panel in bottom-right
	var vp_size = get_viewport().get_visible_rect().size
	_panel.position = Vector2(vp_size.x - PANEL_SIZE.x - 12, vp_size.y - PANEL_SIZE.y - 12)
	_panel.size = PANEL_SIZE

	# Throttle heat map updates
	_update_timer += delta
	if _update_timer < UPDATE_INTERVAL:
		return
	_update_timer = 0.0

	_update_heat_map()
	_update_stats()


func _update_heat_map():
	var channel_data = pheromone_map.get_channel_data(active_channel)
	if channel_data.size() == 0:
		return

	# Smooth-tracked normalization
	var current_max = pheromone_map.get_max_value(active_channel)
	if current_max > _display_max:
		_display_max = current_max
	else:
		_display_max = lerp(_display_max, current_max, 0.1)
	_display_max = max(_display_max, 0.1)

	var w = pheromone_map.get_width()
	var h = pheromone_map.get_height()
	var base_color = CHANNEL_COLORS.get(active_channel, Color.WHITE)

	for gy in range(h):
		for gx in range(w):
			var idx = gy * w + gx
			var value = channel_data[idx]
			var normalized = clamp(value / _display_max, 0.0, 1.0)

			# Gamma boost for visibility
			var intensity = pow(normalized, 0.4)
			var pixel_color = base_color * intensity
			# Dark background for zero regions, colored for active
			pixel_color.a = 0.15 + intensity * 0.85 if intensity > 0.01 else 0.15

			_image.set_pixel(gx, gy, pixel_color)

	_texture.update(_image)


func _update_stats():
	if not pheromone_map:
		return
	var peak = pheromone_map.get_max_value(active_channel)
	var total = pheromone_map.get_total_value(active_channel)
	var w = pheromone_map.get_width()
	var h = pheromone_map.get_height()
	var cells = w * h
	var nonzero = _count_nonzero(active_channel)
	var coverage = (float(nonzero) / float(cells) * 100.0) if cells > 0 else 0.0

	_stats_label.text = "Peak: %.2f  Total: %.0f  Coverage: %.0f%% (%d cells)" % [
		peak, total, coverage, nonzero
	]


func _count_nonzero(channel: int) -> int:
	var data = pheromone_map.get_channel_data(channel)
	var count = 0
	for v in data:
		if v > 0.01:
			count += 1
	return count


func _update_channel_label():
	var ch_name = CHANNEL_NAMES.get(active_channel, "?")
	var group = "Combat" if active_channel < 8 else "Economy"
	var base_color = CHANNEL_COLORS.get(active_channel, Color.WHITE)

	_channel_indicator.color = base_color
	_channel_label.text = "Ch %d: %s  [%s]" % [active_channel, ch_name, group]
	_channel_label.add_theme_color_override("font_color", base_color.lerp(Color.WHITE, 0.3))


func _update_team_label():
	var team_colors = [Color(0.3, 0.6, 1.0), Color(1.0, 0.3, 0.3)]
	var team_names = ["BLUE", "RED"]
	_team_label.text = "Team: %s" % team_names[active_team]
	_team_label.add_theme_color_override("font_color", team_colors[active_team])


func _input(event):
	if not event is InputEventKey or not event.pressed:
		return

	# Channel cycling: , and .
	if event.keycode == KEY_COMMA:
		_cycle_channel(-1)
	elif event.keycode == KEY_PERIOD:
		_cycle_channel(1)

	# Team toggle: T
	elif event.keycode == KEY_T:
		_toggle_team()


func _cycle_channel(dir: int):
	if not pheromone_map:
		return
	var count = pheromone_map.get_channel_count()
	active_channel = (active_channel + dir + count) % count
	_display_max = 1.0  # Reset normalization
	_update_channel_label()


func _toggle_team():
	if not sim:
		return
	active_team = 1 - active_team
	var new_map = sim.get_pheromone_map(active_team)
	if new_map:
		pheromone_map = new_map
		_display_max = 1.0
		_init_heat_map()
		_update_team_label()
		_update_channel_label()


# --- Public API ---

func set_channel(channel: int):
	if pheromone_map and channel >= 0 and channel < pheromone_map.get_channel_count():
		active_channel = channel
		_display_max = 1.0
		_update_channel_label()

func set_team(team: int):
	if team != active_team and sim:
		active_team = team
		var new_map = sim.get_pheromone_map(active_team)
		if new_map:
			pheromone_map = new_map
			_display_max = 1.0
			_init_heat_map()
			_update_team_label()
			_update_channel_label()
