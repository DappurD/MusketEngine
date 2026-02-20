extends Node3D
class_name SoundManager
## Spatial audio manager using Snake's Authentic Gun Sounds pack.

# Preloaded sound pools (populated in _ready)
var _rifle_shots: Array[AudioStream] = []
var _mg_shots: Array[AudioStream] = []
var _sniper_shots: Array[AudioStream] = []
var _pistol_shots: Array[AudioStream] = []
var _reload_sounds: Array[AudioStream] = []
var _impact_stone: Array[AudioStream] = []
var _gear_move: Array[AudioStream] = []  # Movement foley
var _explosions: Array[AudioStream] = []
var _mortar_fire: Array[AudioStream] = []
var _mortar_whistle: Array[AudioStream] = []
var _drone_buzz: Array[AudioStream] = []
var _drone_dive: Array[AudioStream] = []
var _ui_clicks: Array[AudioStream] = []
var _ui_alerts: Array[AudioStream] = []
var _all_shots: Array[AudioStream] = []  # Fallback pool
var _ambient_player: AudioStreamPlayer = null

const SFX_3D_POOL_SIZE: int = 48
const SFX_2D_POOL_SIZE: int = 16
const DEFAULT_BUS_SFX: String = "SFX"
const DEFAULT_BUS_UI: String = "UI"
const DEFAULT_BUS_AMBIENT: String = "Ambient"

var _rng = RandomNumberGenerator.new()
var _players_3d: Array[AudioStreamPlayer3D] = []
var _players_2d: Array[AudioStreamPlayer] = []
var _next_3d_index: int = 0
var _next_2d_index: int = 0
var _recent_sounds: Dictionary = {}
var _category_volumes_db: Dictionary = {
	"master": 0.0,
	"combat": 6.0,
	"impact": 7.0,
	"movement": 4.5,
	"explosion": 8.0,
	"ui": 0.0,
	"ambient": -24.0,
}
var _muted: bool = false

const SOUND_BASE := "res://Snake's Authentic Gun Sounds/"
const AMBIENT_BASE := "res://InMotionAudio - Sinister Textures 1/"
const IMPACT_BASE := "res://BluezoneCorp - Stone Impact/"
const FOLEY_BASE := "res://InMotionAudio - Mask Foley/"
const EXPLOSION_BASE := "res://DavidDumais - Explosion SFX Pack/"
const UI_BASE_BLUE := "res://BluezoneCorp - Futuristic User Interface/"
const UI_BASE_DOEX := "res://Doex Studio - Qantum UI/"


func _ready() -> void:
	_rng.randomize()
	# Load isolated single shots (cleaner for gameplay)
	_rifle_shots = _load_sounds([
		SOUND_BASE + "Full Sound/7.62x39/WAV/762x39 Single WAV.wav",
		SOUND_BASE + "Full Sound/5.56/WAV/556 Single WAV.wav",
	])
	_mg_shots = _load_sounds([
		SOUND_BASE + "Full Sound/7.62x39/WAV/762x39 Burst WAV.wav",
		SOUND_BASE + "Full Sound/5.56/WAV/556 Burst WAV.wav",
	])
	_sniper_shots = _load_sounds([
		SOUND_BASE + "Full Sound/7.62x54R/WAV/762x54r Single WAV.wav",
	])
	_pistol_shots = _load_sounds([
		SOUND_BASE + "Full Sound/.22LR/WAV/22LR Single WAV.wav",
	])

	_reload_sounds = _load_sounds([
		SOUND_BASE + "Reloads, Cycling & More/WAV/AR Reload Full WAV.wav",
		SOUND_BASE + "Reloads, Cycling & More/WAV/AK Reload Full WAV.wav",
		SOUND_BASE + "Reloads, Cycling & More/WAV/308 Magazine Full WAV.wav",
		SOUND_BASE + "Reloads, Cycling & More/WAV/Pump Reload Full WAV.wav",
	])
	
	_impact_stone = _load_sounds([
		IMPACT_BASE + "Bluezone_BC0297_stone_impact_015.wav",
		IMPACT_BASE + "Bluezone_BC0297_stone_impact_041.wav",
		IMPACT_BASE + "Bluezone_BC0297_stone_impact_hammer_015.wav",
	])
	
	_gear_move = _load_sounds([
		FOLEY_BASE + "FOLYMisc_FabricMask_Movement06_InMotionAudio_MaskFoley.wav",
		FOLEY_BASE + "FOLYMisc_3MMask_Movement04_InMotionAudio_MaskFoley.wav",
	])
	
	_explosions = _load_sounds([
		EXPLOSION_BASE + "EXPLReal_Medium Realistic Explosion 15_DDUMAIS_NONE.wav",
		EXPLOSION_BASE + "EXPLDsgn_Nuclear Explosion 07_DDUMAIS_NONE.wav",
	])
	_mortar_fire = _load_sounds([
		EXPLOSION_BASE + "EXPLReal_Medium Realistic Explosion 15_DDUMAIS_NONE.wav",
	])
	_mortar_whistle = _load_sounds([
		FOLEY_BASE + "FOLYMisc_FabricMask_Movement06_InMotionAudio_MaskFoley.wav",
	])
	_drone_buzz = _load_sounds([
		FOLEY_BASE + "FOLYMisc_3MMask_Movement04_InMotionAudio_MaskFoley.wav",
	])
	_drone_dive = _load_sounds([
		UI_BASE_BLUE + "Bluezone_BC0303_futuristic_user_interface_high_tech_beep_038.wav",
	])
	
	_ui_clicks = _load_sounds([
		UI_BASE_DOEX + "UI_Select_Plastic_05.wav",
		UI_BASE_BLUE + "Bluezone_BC0303_futuristic_user_interface_transition_006.wav",
	])
	
	_ui_alerts = _load_sounds([
		UI_BASE_BLUE + "Bluezone_BC0303_futuristic_user_interface_alert_003.wav",
		UI_BASE_BLUE + "Bluezone_BC0303_futuristic_user_interface_high_tech_beep_038.wav",
	])

	# Build fallback pool from all loaded sounds
	_all_shots = _rifle_shots + _mg_shots + _sniper_shots + _pistol_shots

	if _all_shots.is_empty():
		push_warning("[SoundManager] No gun sounds loaded! Check file paths.")
	else:
		print("[SoundManager] Loaded %d gun sound streams." % _all_shots.size())

	_create_player_pools()
	# Ambient loop intentionally disabled (user request: remove random background noise).
	_disable_ambient_loop()


func _process(delta: float) -> void:
	if _recent_sounds.is_empty():
		_update_ambient_mix()
		return

	var to_erase: Array[String] = []
	for key in _recent_sounds.keys():
		var left: float = float(_recent_sounds[key]) - delta
		if left <= 0.0:
			to_erase.append(key)
		else:
			_recent_sounds[key] = left
	for key in to_erase:
		_recent_sounds.erase(key)

	_update_ambient_mix()


func _create_player_pools() -> void:
	for i in SFX_3D_POOL_SIZE:
		var player = AudioStreamPlayer3D.new()
		player.name = "SFX3D_%d" % i
		player.unit_size = 3.0
		player.max_distance = 260.0
		player.attenuation_model = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
		player.bus = _pick_bus(DEFAULT_BUS_SFX)
		add_child(player)
		_players_3d.append(player)

	for i in SFX_2D_POOL_SIZE:
		var player = AudioStreamPlayer.new()
		player.name = "SFX2D_%d" % i
		player.bus = _pick_bus(DEFAULT_BUS_UI)
		add_child(player)
		_players_2d.append(player)


func _pick_bus(preferred_bus: String) -> String:
	return preferred_bus if AudioServer.get_bus_index(preferred_bus) != -1 else "Master"


func _play_ambient_loop() -> void:
	var path = AMBIENT_BASE + "DSGNErie_EerieBoilerRoom06_InMotionAudio_Sinister Textures Volume 1.wav"
	if ResourceLoader.exists(path):
		var stream = ResourceLoader.load(path) as AudioStream
		if stream == null:
			push_warning("[SoundManager] Ambient stream load failed: " + path)
			return
		_ambient_player = AudioStreamPlayer.new()
		_ambient_player.stream = stream
		_ambient_player.volume_db = -12.0
		_ambient_player.bus = _pick_bus(DEFAULT_BUS_AMBIENT)
		_ambient_player.autoplay = false
		add_child(_ambient_player)
		_ambient_player.play()
		_update_ambient_mix()
		print("[SoundManager] Started ambient loop: EerieBoilerRoom06")
	else:
		push_warning("[SoundManager] Ambient file missing: " + path)


func _disable_ambient_loop() -> void:
	if _ambient_player != null:
		if _ambient_player.playing:
			_ambient_player.stop()
		_ambient_player.queue_free()
		_ambient_player = null


func _update_ambient_mix() -> void:
	if _ambient_player == null:
		return
	var darkness: float = 0.0
	if Engine.has_singleton("DayNightCycle") or has_node("/root/DayNightCycle"):
		darkness = clampf(DayNightCycle.get_darkness(), 0.0, 1.0)
	var ambient_boost: float = lerpf(-3.0, 3.5, darkness)
	var muted_offset: float = -80.0 if _muted else 0.0
	_ambient_player.volume_db = -14.0 + ambient_boost + _category_volumes_db["master"] + _category_volumes_db["ambient"] + muted_offset
	_ambient_player.pitch_scale = lerpf(0.97, 1.03, darkness)


## Loads an array of audio streams from the given file paths, warning on failures.
## Uses ResourceLoader.exists() first to avoid hard errors on un-imported files.
func _load_sounds(paths: Array) -> Array[AudioStream]:
	var result: Array[AudioStream] = []
	for path in paths:
		if not ResourceLoader.exists(path):
			push_warning("[SoundManager] File not imported: " + path)
			continue
		var stream = ResourceLoader.load(path) as AudioStream
		if stream != null:
			result.append(stream)
		else:
			push_warning("[SoundManager] Could not load: " + path)
	return result


## Picks a random stream from the pool, falling back to _all_shots if pool is empty.
func _pick_random(pool: Array[AudioStream]) -> AudioStream:
	if pool.is_empty():
		if _all_shots.is_empty():
			return null
		return _all_shots[randi() % _all_shots.size()]
	return pool[randi() % pool.size()]


# ── Public API ────────────────────────────────────────────────────

func set_muted(muted: bool) -> void:
	_muted = muted
	if _muted:
		for p in _players_3d:
			if p.playing:
				p.stop()
		for p in _players_2d:
			if p.playing:
				p.stop()
	_update_ambient_mix()


func toggle_mute() -> bool:
	set_muted(not _muted)
	return _muted


func set_master_volume_db(volume_db: float) -> void:
	_category_volumes_db["master"] = clampf(volume_db, -24.0, 12.0)
	_update_ambient_mix()


func set_category_volume_db(category: String, volume_db: float) -> bool:
	if not _category_volumes_db.has(category):
		return false
	_category_volumes_db[category] = clampf(volume_db, -24.0, 12.0)
	if category == "ambient":
		_update_ambient_mix()
	return true


func get_category_volume_db(category: String) -> float:
	return float(_category_volumes_db.get(category, 0.0))


func play_gunshot(pos: Vector3, weapon_type: String = "rifle") -> void:
	var stream: AudioStream
	match weapon_type:
		"mg":
			stream = _pick_random(_mg_shots)
		"marksman", "sniper", "at":
			stream = _pick_random(_sniper_shots)
		"medic", "pistol":
			stream = _pick_random(_pistol_shots)
		_:
			stream = _pick_random(_rifle_shots)
	_play_at(pos, stream, 1.0, "combat", "gun_%s" % weapon_type, 0.005, 0.92, 1.08, 320.0)


func play_hit_flesh(pos: Vector3) -> void:
	# Use a quieter, shorter shot sound as impact placeholder
	var stream = _pick_random(_pistol_shots)
	_play_at(pos, stream, -9.0, "impact", "flesh", 0.01, 0.95, 1.08, 220.0)


func play_hit_cover(pos: Vector3) -> void:
	var stream: AudioStream
	if not _impact_stone.is_empty():
		stream = _pick_random(_impact_stone)
	else:
		stream = _pick_random(_pistol_shots)
	_play_at(pos, stream, -1.0, "impact", "cover", 0.004, 0.92, 1.08, 260.0)


func play_death(pos: Vector3) -> void:
	var stream = _pick_random(_rifle_shots)
	_play_at(pos, stream, -5.0, "impact", "death", 0.02, 0.92, 1.04, 230.0)


func play_downed(pos: Vector3) -> void:
	var stream = _pick_random(_pistol_shots)
	_play_at(pos, stream, -9.0, "impact", "downed", 0.02, 0.95, 1.06, 210.0)


func play_reload(pos: Vector3) -> void:
	var stream = _pick_random(_reload_sounds)
	_play_at(pos, stream, -2.0, "combat", "reload", 0.02, 0.9, 1.05, 180.0)


func play_footstep(pos: Vector3) -> void:
	# Use gear foley as footstep placeholder (better than silence)
	var stream = _pick_random(_gear_move)
	_play_at(pos, stream, -10.0, "movement", "footstep", 0.03, 0.93, 1.1, 120.0)


func play_explosion(pos: Vector3) -> void:
	var stream = _pick_random(_explosions)
	_play_at(pos, stream, 4.0, "explosion", "explosion", 0.05, 0.95, 1.04, 420.0)


func play_mortar_fire(pos: Vector3) -> void:
	var stream = _pick_random(_mortar_fire)
	_play_at(pos, stream, 2.0, "explosion", "mortar_fire", 0.08, 0.92, 1.02, 420.0)


func play_mortar_whistle(pos: Vector3) -> void:
	var stream = _pick_random(_mortar_whistle)
	_play_at(pos, stream, -5.0, "explosion", "mortar_whistle", 0.06, 0.75, 0.88, 360.0)


func play_drone_buzz(pos: Vector3) -> void:
	var stream = _pick_random(_drone_buzz)
	_play_at(pos, stream, -12.0, "movement", "drone_buzz", 0.04, 0.95, 1.08, 200.0)


func play_drone_dive(pos: Vector3) -> void:
	var stream = _pick_random(_drone_dive)
	_play_at(pos, stream, -2.0, "combat", "drone_dive", 0.08, 0.95, 1.08, 280.0)


func play_ui_click() -> void:
	var stream = _pick_random(_ui_clicks)
	_play_2d(stream, -10.0, "ui", "ui_click", 0.015, 0.96, 1.04)


func play_ui_alert() -> void:
	var stream = _pick_random(_ui_alerts)
	_play_2d(stream, -8.0, "ui", "ui_alert", 0.03, 0.97, 1.03)


## Spawns a one-shot AudioStreamPlayer3D at the given world position with the specified volume.
func _play_at(
	pos: Vector3,
	stream: AudioStream,
	base_volume_db: float,
	category: String,
	throttle_key: String,
	min_interval: float,
	pitch_min: float,
	pitch_max: float,
	max_distance: float
) -> void:
	if stream == null:
		return
	if _muted:
		return
	if _is_throttled(throttle_key, min_interval):
		return
	if _players_3d.is_empty():
		return

	var player = _players_3d[_next_3d_index]
	_next_3d_index = (_next_3d_index + 1) % _players_3d.size()
	if player.playing:
		player.stop()
	player.stream = stream
	player.bus = _pick_bus(DEFAULT_BUS_SFX)
	player.max_distance = maxf(max_distance, 220.0)
	player.volume_db = base_volume_db + _category_volumes_db["master"] + float(_category_volumes_db.get(category, 0.0))
	player.pitch_scale = _rng.randf_range(pitch_min, pitch_max)
	player.global_position = pos
	player.play()


## Spawns a one-shot AudioStreamPlayer (2D) for UI sounds.
func _play_2d(
	stream: AudioStream,
	base_volume_db: float,
	category: String,
	throttle_key: String,
	min_interval: float,
	pitch_min: float,
	pitch_max: float
) -> void:
	if stream == null:
		return
	if _muted:
		return
	if _is_throttled(throttle_key, min_interval):
		return
	if _players_2d.is_empty():
		return

	var player = _players_2d[_next_2d_index]
	_next_2d_index = (_next_2d_index + 1) % _players_2d.size()
	if player.playing:
		player.stop()
	player.stream = stream
	player.bus = _pick_bus(DEFAULT_BUS_UI)
	player.volume_db = base_volume_db + _category_volumes_db["master"] + float(_category_volumes_db.get(category, 0.0))
	player.pitch_scale = _rng.randf_range(pitch_min, pitch_max)
	player.play()


func _is_throttled(key: String, min_interval: float) -> bool:
	var clamped_interval: float = maxf(0.0, min_interval)
	if clamped_interval <= 0.0:
		return false
	if _recent_sounds.has(key):
		return true
	_recent_sounds[key] = clamped_interval
	return false
