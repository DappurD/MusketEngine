extends Node
## Preloads and provides access to all 3D model assets (.gltf).
## Replaces SpriteManager for the 3D pipeline.

# ── Nature Kit (trees, bushes, rocks, grass, etc.) ──────────────────
var nature_trees: Array[PackedScene] = []
var nature_dead_trees: Array[PackedScene] = []
var nature_pines: Array[PackedScene] = []
var nature_bushes: Array[PackedScene] = []
var nature_grass: Array[PackedScene] = []
var nature_flowers: Array[PackedScene] = []
var nature_mushrooms: Array[PackedScene] = []
var nature_rocks: Array[PackedScene] = []
var nature_pebbles: Array[PackedScene] = []
var nature_plants: Array[PackedScene] = []

# ── Toon Shooter Kit (characters, guns, environment) ───────────────
var characters: Dictionary = {}    # name → PackedScene
var guns: Dictionary = {}          # name → PackedScene
var shooter_env: Dictionary = {}   # name → PackedScene (barriers, crates, etc.)

# ── Zombie Kit (enemies, vehicles, weapons, environment) ───────────
var zombies: Dictionary = {}       # name → PackedScene
var zombie_characters: Dictionary = {} # name → PackedScene
var vehicles: Dictionary = {}      # name → PackedScene
var zombie_weapons: Dictionary = {} # name → PackedScene
var zombie_env: Dictionary = {}    # name → PackedScene

# ── Fantasy Props (barrels, furniture, misc) ───────────────────────
var props: Dictionary = {}         # name → PackedScene

# ── Base paths ─────────────────────────────────────────────────────
const NATURE_PATH := "res://glTF/"
const SHOOTER_PATH := "res://Toon Shooter Game Kit - Dec 2022/"
const ZOMBIE_PATH := "res://Zombie Apocalypse Kit - March 2024/"
const PROPS_PATH := "res://Exports/glTF/"

func _ready() -> void:
	_load_nature()
	_load_shooter_kit()
	_load_zombie_kit()
	_load_props()
	print("[AssetManager3D] Loaded:")
	print("  Nature trees: ", nature_trees.size())
	print("  Dead trees: ", nature_dead_trees.size())
	print("  Pines: ", nature_pines.size())
	print("  Bushes: ", nature_bushes.size())
	print("  Grass: ", nature_grass.size())
	print("  Flowers: ", nature_flowers.size())
	print("  Mushrooms: ", nature_mushrooms.size())
	print("  Rocks: ", nature_rocks.size())
	print("  Pebbles: ", nature_pebbles.size())
	print("  Plants: ", nature_plants.size())
	print("  Characters: ", characters.size())
	print("  Guns: ", guns.size())
	print("  Shooter env: ", shooter_env.size())
	print("  Zombies: ", zombies.size())
	print("  Zombie chars: ", zombie_characters.size())
	print("  Vehicles: ", vehicles.size())
	print("  Props: ", props.size())

# ── Nature Kit ─────────────────────────────────────────────────────
## Loads all Nature Mega Kit assets (trees, bushes, grass, rocks, flowers, etc.).
func _load_nature() -> void:
	var tree_names = ["CommonTree_1", "CommonTree_2", "CommonTree_3", "CommonTree_4", "CommonTree_5"]
	for n in tree_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_trees.append(s)

	var dead_names = ["DeadTree_1", "DeadTree_2", "DeadTree_3", "DeadTree_4", "DeadTree_5"]
	for n in dead_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_dead_trees.append(s)

	var pine_names = ["Pine_1", "Pine_2", "Pine_3", "Pine_4", "Pine_5"]
	for n in pine_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_pines.append(s)

	var bush_names = ["Bush_Common", "Bush_Common_Flowers"]
	for n in bush_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_bushes.append(s)

	var grass_names = ["Grass_Common_Short", "Grass_Common_Tall", "Grass_Wispy_Short", "Grass_Wispy_Tall"]
	for n in grass_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_grass.append(s)

	var flower_names = ["Flower_3_Group", "Flower_3_Single", "Flower_4_Group", "Flower_4_Single"]
	for n in flower_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_flowers.append(s)

	var mushroom_names = ["Mushroom_Common", "Mushroom_Laetiporus"]
	for n in mushroom_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_mushrooms.append(s)

	# Rocks (medium)
	var rock_names = ["Rock_Medium_1", "Rock_Medium_2", "Rock_Medium_3"]
	for n in rock_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_rocks.append(s)

	# Pebbles (small)
	var pebble_names = ["Pebble_Round_1", "Pebble_Round_2", "Pebble_Round_3",
		"Pebble_Round_4", "Pebble_Round_5",
		"Pebble_Square_1", "Pebble_Square_2", "Pebble_Square_3",
		"Pebble_Square_4", "Pebble_Square_5", "Pebble_Square_6"]
	for n in pebble_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_pebbles.append(s)

	var plant_names = ["Plant_1", "Plant_1_Big", "Plant_7", "Plant_7_Big", "Fern_1"]
	for n in plant_names:
		var s = _try_load(NATURE_PATH + n + ".gltf")
		if s: nature_plants.append(s)

# ── Toon Shooter Kit ──────────────────────────────────────────────
## Loads Toon Shooter Kit assets (characters, guns, environment barriers/crates).
func _load_shooter_kit() -> void:
	# Characters
	var char_names = ["Character_Soldier", "Character_Hazmat", "Character_Enemy"]
	for n in char_names:
		var s = _try_load(SHOOTER_PATH + "Characters/glTF/" + n + ".gltf")
		if s: characters[n] = s

	# Guns
	var gun_names = ["AK", "Pistol", "Revolver", "Revolver_Small", "Shotgun", "SMG",
		"Sniper", "Sniper_2", "ShortCannon", "RocketLauncher", "GrenadeLauncher",
		"Knife_1", "Knife_2", "Shovel", "FireGrenade", "Grenade"]
	for n in gun_names:
		var s = _try_load(SHOOTER_PATH + "Guns/glTF/" + n + ".gltf")
		if s: guns[n] = s

	# Environment pieces
	var env_names = ["Barrier_Fixed", "Barrier_Large", "Barrier_Single", "Barrier_Trash",
		"BearTrap_Closed", "BearTrap_Open", "BrickWall_1", "BrickWall_2",
		"CardboardBoxes_1", "CardboardBoxes_2", "CardboardBoxes_3", "CardboardBoxes_4",
		"Container_Long", "Container_Small", "Crate",
		"Debris_BrokenCar", "Debris_Papers_1", "Debris_Papers_2"]
	for n in env_names:
		var s = _try_load(SHOOTER_PATH + "Environment/glTF/" + n + ".gltf")
		if s: shooter_env[n] = s

# ── Zombie Apocalypse Kit ─────────────────────────────────────────
## Loads Zombie Apocalypse Kit assets (zombies, characters, vehicles, weapons, environment).
func _load_zombie_kit() -> void:
	# Player characters
	var char_names = ["Characters_Matt", "Characters_Lis", "Characters_Sam",
		"Characters_Shaun", "Characters_Matt_SingleWeapon",
		"Characters_Lis_SingleWeapon", "Characters_Sam_SingleWeapon",
		"Characters_Shaun_SingleWeapon"]
	for n in char_names:
		var s = _try_load(ZOMBIE_PATH + "Characters/glTF/" + n + ".gltf")
		if s: zombie_characters[n] = s

	# Animals
	var animal_names = ["Characters_GermanShepherd", "Characters_Pug"]
	for n in animal_names:
		var s = _try_load(ZOMBIE_PATH + "Characters/glTF/" + n + ".gltf")
		if s: zombie_characters[n] = s

	# Zombies
	var zombie_names = ["Zombie_Basic", "Zombie_Arm", "Zombie_Chubby", "Zombie_Ribcage"]
	for n in zombie_names:
		var s = _try_load(ZOMBIE_PATH + "Characters/glTF/" + n + ".gltf")
		if s: zombies[n] = s

	# Vehicles
	var vehicle_names = ["Vehicle_Pickup", "Vehicle_Pickup_Armored",
		"Vehicle_Sports", "Vehicle_Sports_Armored",
		"Vehicle_Truck", "Vehicle_Truck_Armored"]
	for n in vehicle_names:
		var s = _try_load(ZOMBIE_PATH + "Vehicles/glTF/" + n + ".gltf")
		if s: vehicles[n] = s

	# Weapons
	var weapon_names = ["Axe", "Guitar", "Knife", "Pistol", "Rifle",
		"Shotgun", "SMG", "Spear", "WoodenBat_Barbed", "WoodenBat_Saw"]
	for n in weapon_names:
		var s = _try_load(ZOMBIE_PATH + "Weapons/glTF/" + n + ".gltf")
		if s: zombie_weapons[n] = s

	# Environment
	var env_names = ["Barrel", "Blood_1", "Blood_2", "Blood_3",
		"Chest", "Chest_Special", "CinderBlock", "Container_Green", "Container_Red",
		"Couch", "FireHydrant", "Pallet", "Pallet_Broken",
		"Pipes", "PlasticBarrier", "StreetLights", "TownSign",
		"TrafficBarrier_1", "TrafficBarrier_2",
		"TrafficCone_1", "TrafficCone_2",
		"TrafficLight_1", "TrafficLight_2",
		"TrashBag_1", "TrashBag_2", "WaterTower",
		"Wheel", "Wheels_Stack"]
	for n in env_names:
		var s = _try_load(ZOMBIE_PATH + "Environment/glTF/" + n + ".gltf")
		if s: zombie_env[n] = s

# ── Fantasy Props ─────────────────────────────────────────────────
## Loads Fantasy Props MegaKit assets (barrels, furniture, misc props).
func _load_props() -> void:
	var prop_names = ["Anvil", "Anvil_Log", "Bag", "Banner_1", "Banner_2",
		"Barrel", "Barrel_Apples", "Barrel_Holder",
		"Bed_Twin1", "Bed_Twin2", "Bench", "Bookcase_2",
		"BookGroup_Medium_1", "BookGroup_Medium_2", "BookGroup_Medium_3",
		"BookGroup_Small_1", "BookGroup_Small_2"]
	for n in prop_names:
		var s = _try_load(PROPS_PATH + n + ".gltf")
		if s: props[n] = s

# ── Helpers ────────────────────────────────────────────────────────
## Attempts to load a PackedScene from the given path; returns null and warns if missing.
func _try_load(path: String) -> PackedScene:
	if ResourceLoader.exists(path):
		return load(path) as PackedScene
	else:
		push_warning("[AssetManager3D] Missing: " + path)
		return null

## Returns a random PackedScene from the given array, or null if empty.
func get_random(arr: Array[PackedScene]) -> PackedScene:
	if arr.is_empty(): return null
	return arr[randi() % arr.size()]

## Returns a random common tree scene from the Nature kit.
func get_random_tree() -> PackedScene:
	return get_random(nature_trees)

func get_random_pine() -> PackedScene:
	return get_random(nature_pines)

func get_random_dead_tree() -> PackedScene:
	return get_random(nature_dead_trees)

func get_random_bush() -> PackedScene:
	return get_random(nature_bushes)

func get_random_rock() -> PackedScene:
	return get_random(nature_rocks)

func get_random_grass() -> PackedScene:
	return get_random(nature_grass)

func get_random_flower() -> PackedScene:
	return get_random(nature_flowers)

func get_random_pebble() -> PackedScene:
	return get_random(nature_pebbles)

func get_random_plant() -> PackedScene:
	return get_random(nature_plants)

func get_random_mushroom() -> PackedScene:
	return get_random(nature_mushrooms)

func get_character(name_key: String) -> PackedScene:
	return characters.get(name_key)

func get_gun(name_key: String) -> PackedScene:
	return guns.get(name_key)

func get_zombie(name_key: String) -> PackedScene:
	return zombies.get(name_key)

func get_vehicle(name_key: String) -> PackedScene:
	return vehicles.get(name_key)

func get_prop(name_key: String) -> PackedScene:
	return props.get(name_key)

## Instantiate a model and return it as a Node3D ready to add_child.
func instantiate(scene: PackedScene) -> Node3D:
	if scene == null: return null
	return scene.instantiate() as Node3D
