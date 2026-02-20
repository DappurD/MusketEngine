extends VBoxContainer
## Event ticker — bottom-left scrolling log of significant battlefield events.
## Subtle dark panel background, opacity-graded entries (newest brightest).

var _ui: BattleCommandUI
var _expanded: bool = false
var _events: Array[Dictionary] = []  # {text: String, color: Color, time: float}
var _visible_labels: Array[Label] = []
var _scroll_container: ScrollContainer
var _expanded_vbox: VBoxContainer
var _compact_panel: PanelContainer
var _compact_vbox: VBoxContainer
var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.5
const MAX_EVENTS: int = 100
const VISIBLE_LINES: int = 4
const EXPANDED_LINES: int = 20


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	# Position: bottom-left, above bottom edge
	anchors_preset = Control.PRESET_BOTTOM_LEFT
	offset_left = 10.0
	offset_bottom = -10.0
	custom_minimum_size = Vector2(380, 0)

	add_theme_constant_override("separation", 0)

	# ── Compact view (4 lines in a subtle panel) ──
	_compact_panel = PanelContainer.new()
	_compact_panel.custom_minimum_size = Vector2(380, 0)
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.03, 0.035, 0.05, 0.75)
	sb.corner_radius_top_left = 4
	sb.corner_radius_top_right = 4
	sb.corner_radius_bottom_left = 4
	sb.corner_radius_bottom_right = 4
	sb.content_margin_left = 8.0
	sb.content_margin_right = 8.0
	sb.content_margin_top = 5.0
	sb.content_margin_bottom = 5.0
	_compact_panel.add_theme_stylebox_override("panel", sb)
	add_child(_compact_panel)

	_compact_vbox = VBoxContainer.new()
	_compact_vbox.add_theme_constant_override("separation", 1)
	_compact_panel.add_child(_compact_vbox)

	# Header
	var header := Label.new()
	header.text = "EVENTS  [J expand]"
	header.add_theme_font_size_override("font_size", 9)
	header.add_theme_color_override("font_color", Color(0.4, 0.45, 0.5))
	_compact_vbox.add_child(header)

	for i in VISIBLE_LINES:
		var lbl := Label.new()
		lbl.text = ""
		lbl.add_theme_font_size_override("font_size", 11)
		lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
		lbl.autowrap_mode = TextServer.AUTOWRAP_OFF
		lbl.clip_text = true
		lbl.custom_minimum_size = Vector2(360, 0)
		_compact_vbox.add_child(lbl)
		_visible_labels.append(lbl)

	# ── Expanded scroll view (hidden by default) ──
	_scroll_container = ScrollContainer.new()
	_scroll_container.custom_minimum_size = Vector2(380, 320)
	_scroll_container.visible = false

	var panel := PanelContainer.new()
	var esb := StyleBoxFlat.new()
	esb.bg_color = Color(0.04, 0.05, 0.07, 0.94)
	esb.border_width_top = 1
	esb.border_width_bottom = 1
	esb.border_width_left = 1
	esb.border_width_right = 1
	esb.border_color = Color(0.15, 0.18, 0.22)
	esb.corner_radius_top_left = 6
	esb.corner_radius_top_right = 6
	esb.corner_radius_bottom_left = 6
	esb.corner_radius_bottom_right = 6
	esb.content_margin_left = 10.0
	esb.content_margin_right = 10.0
	esb.content_margin_top = 8.0
	esb.content_margin_bottom = 8.0
	esb.shadow_color = Color(0, 0, 0, 0.3)
	esb.shadow_size = 6
	esb.shadow_offset = Vector2(0, 2)
	panel.add_theme_stylebox_override("panel", esb)
	_scroll_container.add_child(panel)

	_expanded_vbox = VBoxContainer.new()
	_expanded_vbox.add_theme_constant_override("separation", 2)
	panel.add_child(_expanded_vbox)

	add_child(_scroll_container)


func toggle_expanded() -> void:
	_expanded = not _expanded
	_scroll_container.visible = _expanded
	_compact_panel.visible = not _expanded

	if _expanded:
		_rebuild_expanded_view()


func add_event(text: String, color: Color = BattleCommandUI.TEXT_PRIMARY) -> void:
	var game_time: float = 0.0
	if _ui and _ui.sim:
		game_time = _ui.sim.get_game_time()

	_events.push_front({"text": text, "color": color, "time": game_time})
	if _events.size() > MAX_EVENTS:
		_events.pop_back()

	_update_visible()
	if _expanded:
		_rebuild_expanded_view()


func _update_visible() -> void:
	for i in VISIBLE_LINES:
		if i < _events.size():
			var ev: Dictionary = _events[i]
			var time_str: String = BattleCommandUI.format_time(ev["time"])
			_visible_labels[i].text = "[%s] %s" % [time_str, ev["text"]]
			# Fade older entries
			var alpha: float = lerpf(1.0, 0.4, float(i) / float(VISIBLE_LINES))
			_visible_labels[i].add_theme_color_override("font_color",
				Color(ev["color"], alpha))
		else:
			_visible_labels[i].text = ""


func _rebuild_expanded_view() -> void:
	# Clear existing
	for child in _expanded_vbox.get_children():
		child.queue_free()

	# Header
	var header := Label.new()
	header.text = "EVENT LOG  [J to close]"
	header.add_theme_font_size_override("font_size", 12)
	header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	_expanded_vbox.add_child(header)

	var count: int = mini(_events.size(), EXPANDED_LINES)
	for i in count:
		var ev: Dictionary = _events[i]
		var lbl := Label.new()
		var time_str: String = BattleCommandUI.format_time(ev["time"])
		lbl.text = "[%s] %s" % [time_str, ev["text"]]
		lbl.add_theme_font_size_override("font_size", 11)
		var alpha: float = lerpf(1.0, 0.5, float(i) / float(count))
		lbl.add_theme_color_override("font_color", Color(ev["color"], alpha))
		lbl.autowrap_mode = TextServer.AUTOWRAP_WORD
		_expanded_vbox.add_child(lbl)


func update_data(delta: float) -> void:
	_update_timer += delta
	if _update_timer < UPDATE_INTERVAL:
		return
	_update_timer = 0.0

	if not _ui or not _ui.sim:
		return

	_detect_events()


func _detect_events() -> void:
	var sim: SimulationServer = _ui.sim
	var stats: Dictionary = sim.get_debug_stats()

	var alive1: int = sim.get_alive_count_for_team(1)
	var alive2: int = sim.get_alive_count_for_team(2)
	var total: int = sim.get_unit_count() / 2
	if total <= 0:
		total = 1

	# Detect sim restart: alive jumped up significantly (new battle)
	if alive1 > _prev_alive_t1 + 10 or alive2 > _prev_alive_t2 + 10:
		_first_blood_fired = false
		_battle_over_fired = false
		_prev_morale_t1 = 1.0
		_prev_morale_t2 = 1.0
		_prev_ratio_t1 = 1.0
		_prev_ratio_t2 = 1.0
		_prev_captures_t1 = 0
		_prev_captures_t2 = 0
		_prev_alive_t1 = alive1
		_prev_alive_t2 = alive2
		add_event("Battle started — %d vs %d" % [alive1, alive2], BattleCommandUI.ACCENT_COLOR)
		return

	# ── First blood ──
	if not _first_blood_fired and _prev_alive_t1 > 0:
		if alive1 < _prev_alive_t1 or alive2 < _prev_alive_t2:
			_first_blood_fired = true
			if alive1 < _prev_alive_t1:
				add_event("First blood — Team 2 draws first!", BattleCommandUI.TEAM2_COLOR)
			else:
				add_event("First blood — Team 1 draws first!", BattleCommandUI.TEAM1_COLOR)

	# ── Mass casualty (5+ in one tick) ──
	if _prev_alive_t1 > 0:
		var lost1: int = _prev_alive_t1 - alive1
		var lost2: int = _prev_alive_t2 - alive2
		if lost1 >= 5:
			add_event("Team 1 lost %d units!" % lost1, Color(1.0, 0.5, 0.1))
		if lost2 >= 5:
			add_event("Team 2 lost %d units!" % lost2, Color(1.0, 0.5, 0.1))

	# ── Morale thresholds (from cached per-unit computation) ──
	var m1: float = _ui.cached_morale_t1
	var m2: float = _ui.cached_morale_t2
	if _prev_morale_t1 >= 0.3 and m1 < 0.3 and alive1 > 0:
		add_event("Team 1 morale breaking!", BattleCommandUI.WARNING_COLOR)
	if _prev_morale_t2 >= 0.3 and m2 < 0.3 and alive2 > 0:
		add_event("Team 2 morale breaking!", BattleCommandUI.WARNING_COLOR)
	if _prev_morale_t1 >= 0.1 and m1 < 0.1 and alive1 > 0:
		add_event("Team 1 morale collapsed!", BattleCommandUI.CRITICAL_COLOR)
	if _prev_morale_t2 >= 0.1 and m2 < 0.1 and alive2 > 0:
		add_event("Team 2 morale collapsed!", BattleCommandUI.CRITICAL_COLOR)
	_prev_morale_t1 = m1
	_prev_morale_t2 = m2

	# ── Force ratio milestones ──
	var ratio1: float = float(alive1) / float(total)
	var ratio2: float = float(alive2) / float(total)
	if _prev_ratio_t1 > 0.5 and ratio1 <= 0.5 and alive1 > 0:
		add_event("Team 1 at half strength (%d remain)" % alive1, BattleCommandUI.TEAM1_COLOR)
	if _prev_ratio_t2 > 0.5 and ratio2 <= 0.5 and alive2 > 0:
		add_event("Team 2 at half strength (%d remain)" % alive2, BattleCommandUI.TEAM2_COLOR)
	if _prev_ratio_t1 > 0.25 and ratio1 <= 0.25 and alive1 > 0:
		add_event("Team 1 critical — %d survivors" % alive1, BattleCommandUI.CRITICAL_COLOR)
	if _prev_ratio_t2 > 0.25 and ratio2 <= 0.25 and alive2 > 0:
		add_event("Team 2 critical — %d survivors" % alive2, BattleCommandUI.CRITICAL_COLOR)
	_prev_ratio_t1 = ratio1
	_prev_ratio_t2 = ratio2

	# ── Battle over ──
	if not _battle_over_fired and _prev_alive_t1 > 0:
		if alive1 == 0 and alive2 > 0:
			add_event("BATTLE OVER — Team 2 victory!", BattleCommandUI.TEAM2_COLOR)
			_battle_over_fired = true
		elif alive2 == 0 and alive1 > 0:
			add_event("BATTLE OVER — Team 1 victory!", BattleCommandUI.TEAM1_COLOR)
			_battle_over_fired = true
		elif alive1 == 0 and alive2 == 0:
			add_event("BATTLE OVER — Mutual destruction!", BattleCommandUI.WARNING_COLOR)
			_battle_over_fired = true

	_prev_alive_t1 = alive1
	_prev_alive_t2 = alive2

	# ── Capture point changes ──
	if sim.has_method("get_capture_data"):
		var cap_data: Dictionary = sim.get_capture_data()
		if cap_data.has("owner_team"):
			var owners = cap_data["owner_team"]
			var new_t1: int = 0
			var new_t2: int = 0
			for o in owners:
				if o == 0:
					new_t1 += 1
				elif o == 1:
					new_t2 += 1

			if new_t1 > _prev_captures_t1:
				add_event("Team 1 captured a point!", BattleCommandUI.TEAM1_COLOR)
			if new_t2 > _prev_captures_t2:
				add_event("Team 2 captured a point!", BattleCommandUI.TEAM2_COLOR)

			_prev_captures_t1 = new_t1
			_prev_captures_t2 = new_t2

	# ── Mortar events ──
	var mortar_impacts: int = stats.get("mortar_impacts", 0)
	if mortar_impacts > 0:
		add_event("Mortar strike! (%d impacts)" % mortar_impacts, BattleCommandUI.WARNING_COLOR)

	# ── Personality break events ──
	var berserk: int = stats.get("berserk_units", 0)
	var frozen: int = stats.get("frozen_units", 0)
	if berserk > _prev_berserk and berserk > 0:
		add_event("%d unit(s) went BERSERK!" % (berserk - _prev_berserk), Color(1.0, 0.1, 0.1))
	if frozen > _prev_frozen and frozen > 0:
		add_event("%d unit(s) FROZEN with fear!" % (frozen - _prev_frozen), Color(0.3, 0.3, 0.8))
	_prev_berserk = berserk
	_prev_frozen = frozen


# Previous frame stats for delta detection
var _prev_alive_t1: int = 0
var _prev_alive_t2: int = 0
var _prev_captures_t1: int = 0
var _prev_captures_t2: int = 0
var _prev_morale_t1: float = 1.0
var _prev_morale_t2: float = 1.0
var _prev_ratio_t1: float = 1.0
var _prev_ratio_t2: float = 1.0
var _prev_berserk: int = 0
var _prev_frozen: int = 0
var _first_blood_fired: bool = false
var _battle_over_fired: bool = false


## Reset tracking state (called on sim restart via BattleCommandUI)
func reset_tracking() -> void:
	_prev_alive_t1 = 0
	_prev_alive_t2 = 0
	_prev_captures_t1 = 0
	_prev_captures_t2 = 0
	_prev_morale_t1 = 1.0
	_prev_morale_t2 = 1.0
	_prev_ratio_t1 = 1.0
	_prev_ratio_t2 = 1.0
	_prev_berserk = 0
	_prev_frozen = 0
	_first_blood_fired = false
	_battle_over_fired = false
