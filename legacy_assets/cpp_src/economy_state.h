#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <array>
#include <atomic>

namespace godot {

/// Economy state tracking with thread-safe resource management.
///
/// Maintains per-team stockpiles, production rates, and consumption tracking
/// using atomic operations for thread-safe updates from worker threads.
///
/// Integration:
///   - GDScript EconomyState.gd wraps this for high-level API
///   - Worker threads call add_resource() when gathering completes
///   - BuildPlanner calls consume_resources() when construction starts
///
/// Extraction Guide:
///   Replace ResourceType enum with your game's resources. All logic is
///   generic over resource type IDs.
class EconomyStateCPP : public RefCounted {
	GDCLASS(EconomyStateCPP, RefCounted);

public:
	/// Resource types (game-specific - replace as needed)
	enum ResourceType {
		RES_METAL = 0,
		RES_CRYSTAL = 1,
		RES_ENERGY = 2,
		RES_COUNT
	};

	EconomyStateCPP();
	~EconomyStateCPP() = default;

	/// Add resources to team's stockpile (thread-safe).
	void add_resource(int team_id, int resource_type, int amount);

	/// Remove resources from team's stockpile (thread-safe).
	/// Returns true if successful, false if insufficient resources.
	bool consume_resource(int team_id, int resource_type, int amount);

	/// Check if team can afford a resource cost.
	/// cost: Dictionary { resource_type: amount, ... }
	bool can_afford(int team_id, const Dictionary& cost) const;

	/// Get current stockpile for a team.
	/// Returns Dictionary { RES_METAL: 150, RES_CRYSTAL: 80, ... }
	Dictionary get_stockpile(int team_id) const;

	/// Get amount of a specific resource.
	int get_resource_amount(int team_id, int resource_type) const;

	/// Get total resources consumed (lifetime tracking).
	int get_total_consumed(int team_id, int resource_type) const;

	/// Reset all stockpiles to zero (for testing).
	void reset();

protected:
	static void _bind_methods();

private:
	static constexpr int MAX_TEAMS = 2;

	/// Per-team stockpiles [team_id][resource_type] -> amount
	/// Atomic for thread-safe updates from worker threads
	std::array<std::array<std::atomic<int>, RES_COUNT>, MAX_TEAMS> _stockpiles;

	/// Total consumed tracking [team_id][resource_type] -> total
	std::array<std::array<std::atomic<int>, RES_COUNT>, MAX_TEAMS> _total_consumed;

	/// Validate team ID
	bool _is_valid_team(int team_id) const {
		return team_id >= 0 && team_id < MAX_TEAMS;
	}

	/// Validate resource type
	bool _is_valid_resource(int resource_type) const {
		return resource_type >= 0 && resource_type < RES_COUNT;
	}
};

} // namespace godot
