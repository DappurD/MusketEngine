#include "economy_state.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

EconomyStateCPP::EconomyStateCPP() {
	// Initialize all stockpiles and counters to zero
	for (int team = 0; team < MAX_TEAMS; ++team) {
		for (int res = 0; res < RES_COUNT; ++res) {
			_stockpiles[team][res].store(0);
			_total_consumed[team][res].store(0);
		}
	}
}

void EconomyStateCPP::add_resource(int team_id, int resource_type, int amount) {
	if (!_is_valid_team(team_id) || !_is_valid_resource(resource_type)) {
		UtilityFunctions::push_error("EconomyStateCPP::add_resource - Invalid team_id or resource_type");
		return;
	}

	if (amount < 0) {
		UtilityFunctions::push_error("EconomyStateCPP::add_resource - Cannot add negative amount");
		return;
	}

	// Atomic add (thread-safe)
	_stockpiles[team_id][resource_type].fetch_add(amount, std::memory_order_relaxed);
}

bool EconomyStateCPP::consume_resource(int team_id, int resource_type, int amount) {
	if (!_is_valid_team(team_id) || !_is_valid_resource(resource_type)) {
		UtilityFunctions::push_error("EconomyStateCPP::consume_resource - Invalid team_id or resource_type");
		return false;
	}

	if (amount < 0) {
		UtilityFunctions::push_error("EconomyStateCPP::consume_resource - Cannot consume negative amount");
		return false;
	}

	// Atomic compare-exchange loop to ensure we don't go negative
	int current = _stockpiles[team_id][resource_type].load(std::memory_order_relaxed);
	while (true) {
		if (current < amount) {
			return false;  // Insufficient resources
		}

		// Try to atomically subtract
		if (_stockpiles[team_id][resource_type].compare_exchange_weak(
				current, current - amount,
				std::memory_order_relaxed, std::memory_order_relaxed)) {
			// Success - update consumption tracking
			_total_consumed[team_id][resource_type].fetch_add(amount, std::memory_order_relaxed);
			return true;
		}
		// If CAS failed, current was updated with new value, retry loop
	}
}

bool EconomyStateCPP::can_afford(int team_id, const Dictionary& cost) const {
	if (!_is_valid_team(team_id)) {
		return false;
	}

	// Check all required resources
	Array keys = cost.keys();
	for (int i = 0; i < keys.size(); ++i) {
		int res_type = keys[i];
		int required = cost[res_type];

		if (!_is_valid_resource(res_type)) {
			continue;  // Skip invalid resource types
		}

		int available = _stockpiles[team_id][res_type].load(std::memory_order_relaxed);
		if (available < required) {
			return false;  // Can't afford
		}
	}

	return true;
}

Dictionary EconomyStateCPP::get_stockpile(int team_id) const {
	Dictionary result;

	if (!_is_valid_team(team_id)) {
		return result;
	}

	// Read all resource amounts
	for (int res = 0; res < RES_COUNT; ++res) {
		int amount = _stockpiles[team_id][res].load(std::memory_order_relaxed);
		result[res] = amount;
	}

	return result;
}

int EconomyStateCPP::get_resource_amount(int team_id, int resource_type) const {
	if (!_is_valid_team(team_id) || !_is_valid_resource(resource_type)) {
		return 0;
	}

	return _stockpiles[team_id][resource_type].load(std::memory_order_relaxed);
}

int EconomyStateCPP::get_total_consumed(int team_id, int resource_type) const {
	if (!_is_valid_team(team_id) || !_is_valid_resource(resource_type)) {
		return 0;
	}

	return _total_consumed[team_id][resource_type].load(std::memory_order_relaxed);
}

void EconomyStateCPP::reset() {
	for (int team = 0; team < MAX_TEAMS; ++team) {
		for (int res = 0; res < RES_COUNT; ++res) {
			_stockpiles[team][res].store(0, std::memory_order_relaxed);
			_total_consumed[team][res].store(0, std::memory_order_relaxed);
		}
	}
}

void EconomyStateCPP::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_resource", "team_id", "resource_type", "amount"), &EconomyStateCPP::add_resource);
	ClassDB::bind_method(D_METHOD("consume_resource", "team_id", "resource_type", "amount"), &EconomyStateCPP::consume_resource);
	ClassDB::bind_method(D_METHOD("can_afford", "team_id", "cost"), &EconomyStateCPP::can_afford);
	ClassDB::bind_method(D_METHOD("get_stockpile", "team_id"), &EconomyStateCPP::get_stockpile);
	ClassDB::bind_method(D_METHOD("get_resource_amount", "team_id", "resource_type"), &EconomyStateCPP::get_resource_amount);
	ClassDB::bind_method(D_METHOD("get_total_consumed", "team_id", "resource_type"), &EconomyStateCPP::get_total_consumed);
	ClassDB::bind_method(D_METHOD("reset"), &EconomyStateCPP::reset);

	// Bind resource type constants (use BIND_CONSTANT for nested enum values)
	BIND_CONSTANT(RES_METAL);
	BIND_CONSTANT(RES_CRYSTAL);
	BIND_CONSTANT(RES_ENERGY);
	BIND_CONSTANT(RES_COUNT);
}
