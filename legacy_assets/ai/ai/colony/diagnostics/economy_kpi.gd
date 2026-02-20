class_name EconomyKPI
extends RefCounted
## Economy AI performance metrics and KPI tracking.
##
## Tracks economic efficiency: resource throughput, worker utilization,
## construction cycle times. Exports to JSON for headless testing and
## regression validation.
##
## Similar to SimulationServer's tick profiling, but for economy-specific metrics.
##
## Usage:
##   var kpi = EconomyKPI.new()
##   kpi.sample(economy_state, task_allocator, build_planner)  # Call every N seconds
##   var summary = kpi.get_summary()
##   kpi.export_json("test/output/economy_kpi.json")

## Sample data point
class KPISample:
	var timestamp: float  ## Seconds since start
	var stockpile: Dictionary  ## { resource_type: amount }
	var worker_count: int  ## Total workers
	var idle_worker_count: int  ## Workers not assigned
	var active_task_count: int  ## Tasks being executed
	var construction_count: int  ## Active construction sites
	var production_rates: Dictionary  ## { resource_type: rate_per_sec }

	func _init(p_time: float, p_stockpile: Dictionary, p_workers: int, p_idle: int, p_tasks: int, p_construction: int, p_rates: Dictionary):
		timestamp = p_time
		stockpile = p_stockpile
		worker_count = p_workers
		idle_worker_count = p_idle
		active_task_count = p_tasks
		construction_count = p_construction
		production_rates = p_rates


## All samples collected
var _samples: Array[KPISample] = []

## Start time (for timestamp calculation)
var _start_time = 0.0

## Sample interval (seconds between samples)
var _sample_interval = 1.0


func _init():
	_start_time = Time.get_ticks_msec() / 1000.0


## Record current economy state as a sample.
func sample(economy_state: EconomyState, worker_count: int, idle_count: int, task_count: int, construction_count: int, team_id: int = 0) -> void:
	var current_time = Time.get_ticks_msec() / 1000.0
	var elapsed = current_time - _start_time

	var sample = KPISample.new(
		elapsed,
		economy_state.get_stockpile(team_id),
		worker_count,
		idle_count,
		task_count,
		construction_count,
		_get_production_rates(economy_state, team_id)
	)

	_samples.append(sample)


## Get production rates from economy state.
func _get_production_rates(economy_state: EconomyState, team_id: int) -> Dictionary:
	var rates = {}
	for res_type in EconomyState.ResourceType.values():
		if res_type == EconomyState.ResourceType.RES_COUNT:
			continue
		rates[res_type] = economy_state.get_production_rate(team_id, res_type)
	return rates


## Compute aggregate metrics from all samples.
func get_summary() -> Dictionary:
	if _samples.is_empty():
		return {}

	var summary = {
		"sample_count": _samples.size(),
		"duration_seconds": _samples[-1].timestamp if not _samples.is_empty() else 0.0,
		"avg_throughput": _compute_avg_throughput(),
		"worker_utilization": _compute_worker_utilization(),
		"peak_stockpile": _compute_peak_stockpile(),
		"avg_construction_time": _compute_avg_construction_time(),
		"idle_worker_ratio": _compute_idle_ratio(),
	}

	return summary


## Compute average resource throughput (resources gathered per minute).
func _compute_avg_throughput() -> Dictionary:
	if _samples.size() < 2:
		return {}

	var first = _samples[0]
	var last = _samples[-1]
	var duration_min = (last.timestamp - first.timestamp) / 60.0

	if duration_min <= 0.0:
		return {}

	var throughput = {}
	for res_type in last.stockpile:
		var gained = last.stockpile[res_type] - first.stockpile.get(res_type, 0)
		throughput[res_type] = gained / duration_min  # Resources per minute

	return throughput


## Compute worker utilization (avg % of workers actively working).
func _compute_worker_utilization() -> float:
	if _samples.is_empty():
		return 0.0

	var total_utilization = 0.0
	var count = 0

	for sample in _samples:
		if sample.worker_count > 0:
			var active_workers = sample.worker_count - sample.idle_worker_count
			total_utilization += float(active_workers) / float(sample.worker_count)
			count += 1

	return total_utilization / float(count) if count > 0 else 0.0


## Compute peak stockpile for each resource.
func _compute_peak_stockpile() -> Dictionary:
	var peaks = {}

	for sample in _samples:
		for res_type in sample.stockpile:
			var amount = sample.stockpile[res_type]
			if not peaks.has(res_type) or amount > peaks[res_type]:
				peaks[res_type] = amount

	return peaks


## Compute average construction completion time (placeholder - needs task tracking).
func _compute_avg_construction_time() -> float:
	# TODO: Track individual construction start/complete times
	return 0.0  # Placeholder


## Compute idle worker ratio (avg).
func _compute_idle_ratio() -> float:
	if _samples.is_empty():
		return 0.0

	var total_idle_ratio = 0.0

	for sample in _samples:
		if sample.worker_count > 0:
			total_idle_ratio += float(sample.idle_worker_count) / float(sample.worker_count)

	return total_idle_ratio / float(_samples.size())


## Export metrics to JSON file.
func export_json(file_path: String) -> void:
	var summary = get_summary()

	var export_data = {
		"summary": summary,
		"samples": _serialize_samples(),
		"metadata": {
			"start_time": _start_time,
			"sample_interval": _sample_interval,
			"sample_count": _samples.size(),
		}
	}

	var file = FileAccess.open(file_path, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(export_data, "\t"))
		file.close()
	else:
		push_error("EconomyKPI: Failed to open file for writing: " + file_path)


## Serialize samples for JSON export.
func _serialize_samples() -> Array:
	var serialized = []

	for sample in _samples:
		serialized.append({
			"timestamp": sample.timestamp,
			"stockpile": sample.stockpile,
			"worker_count": sample.worker_count,
			"idle_workers": sample.idle_worker_count,
			"active_tasks": sample.active_task_count,
			"constructions": sample.construction_count,
			"production_rates": sample.production_rates,
		})

	return serialized


## Clear all samples (reset).
func reset() -> void:
	_samples.clear()
	_start_time = Time.get_ticks_msec() / 1000.0


## Set sample interval.
func set_sample_interval(interval_sec: float) -> void:
	_sample_interval = interval_sec
