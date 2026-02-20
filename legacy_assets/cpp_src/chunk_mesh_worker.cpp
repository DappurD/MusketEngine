#include "chunk_mesh_worker.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>

using namespace godot;

ChunkMeshWorker::ChunkMeshWorker() {}

ChunkMeshWorker::~ChunkMeshWorker() {
    shutdown();
}

void ChunkMeshWorker::setup(VoxelWorld *world, int num_threads) {
    if (_running) {
        shutdown();
    }

    _world = world;
    if (!_world || !_world->is_initialized()) {
        UtilityFunctions::push_error("[ChunkMeshWorker] World is null or not initialized");
        return;
    }

    num_threads = std::max(1, std::min(num_threads, 16));
    _running = true;
    _pending_count = 0;
    _active_count = 0;

    _threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        _threads.emplace_back(&ChunkMeshWorker::_worker_func, this);
    }

    UtilityFunctions::print("[ChunkMeshWorker] Started ", num_threads, " worker threads");
}

void ChunkMeshWorker::queue_mesh(int cx, int cy, int cz, int lod_level) {
    if (!_running) return;

    {
        std::lock_guard<std::mutex> lock(_job_mutex);
        _job_queue.push_back({cx, cy, cz, lod_level});
    }
    _pending_count++;
    _job_cv.notify_one();
}

void ChunkMeshWorker::queue_mesh_batch(const PackedInt32Array &coords, const Vector3 &camera_pos, bool prioritize_near, int lod_level) {
    if (!_running || !_world) return;

    int count = coords.size() / 3;
    if (count == 0) return;

    float scale = _world->get_voxel_scale();
    float half_x = float(_world->get_world_size_x()) * scale * 0.5f;
    float half_z = float(_world->get_world_size_z()) * scale * 0.5f;

    // Build list of jobs with distances
    struct PrioritizedJob {
        MeshJob job;
        float dist_sq;
    };
    std::vector<PrioritizedJob> jobs;
    jobs.reserve(count);

    for (int i = 0; i < count; i++) {
        int cx = coords[i * 3];
        int cy = coords[i * 3 + 1];
        int cz = coords[i * 3 + 2];

        PrioritizedJob pj;
        pj.job = {cx, cy, cz, lod_level};

        if (prioritize_near) {
            float wx = float(cx * 32 + 16) * scale - half_x;
            float wy = float(cy * 32 + 16) * scale;
            float wz = float(cz * 32 + 16) * scale - half_z;
            float dx = wx - camera_pos.x;
            float dy = wy - camera_pos.y;
            float dz = wz - camera_pos.z;
            pj.dist_sq = dx * dx + dy * dy + dz * dz;
        } else {
            pj.dist_sq = 0.0f;
        }
        jobs.push_back(pj);
    }

    if (prioritize_near) {
        std::sort(jobs.begin(), jobs.end(), [](const PrioritizedJob &a, const PrioritizedJob &b) {
            return a.dist_sq < b.dist_sq;
        });
    }

    {
        std::lock_guard<std::mutex> lock(_job_mutex);
        for (auto &pj : jobs) {
            _job_queue.push_back(pj.job);
        }
    }
    _pending_count += count;
    _job_cv.notify_all();
}

Array ChunkMeshWorker::poll_results(int max_results) {
    Array out;

    std::lock_guard<std::mutex> lock(_result_mutex);

    int count = (int)_result_queue.size();
    if (max_results > 0 && count > max_results) {
        count = max_results;
    }

    for (int i = 0; i < count; i++) {
        MeshResult &r = _result_queue.front();

        Dictionary d;
        d["cx"] = r.cx;
        d["cy"] = r.cy;
        d["cz"] = r.cz;
        d["lod"] = r.lod_level;
        d["empty"] = r.empty;

        if (!r.empty) {
            d["arrays"] = VoxelMesherBlocky::to_godot_arrays(r.mesh);
        }

        out.append(d);
        _result_queue.pop_front();
    }

    return out;
}

int ChunkMeshWorker::get_pending_count() const {
    return _pending_count.load();
}

int ChunkMeshWorker::get_completed_count() const {
    std::lock_guard<std::mutex> lock(_result_mutex);
    return (int)_result_queue.size();
}

bool ChunkMeshWorker::is_idle() const {
    return _pending_count.load() == 0 && _active_count.load() == 0;
}

void ChunkMeshWorker::shutdown() {
    if (!_running) return;

    _running = false;
    _job_cv.notify_all();

    for (auto &t : _threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    _threads.clear();

    // Clear queues
    {
        std::lock_guard<std::mutex> lock(_job_mutex);
        _job_queue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(_result_mutex);
        _result_queue.clear();
    }
    _pending_count = 0;
    _active_count = 0;
}

// ── Worker Thread ────────────────────────────────────────────────────────

void ChunkMeshWorker::_worker_func() {
    while (true) {
        MeshJob job;

        // Wait for a job
        {
            std::unique_lock<std::mutex> lock(_job_mutex);
            _job_cv.wait(lock, [this]() {
                return !_job_queue.empty() || !_running;
            });

            if (!_running && _job_queue.empty()) {
                return; // Shutdown
            }

            job = _job_queue.front();
            _job_queue.pop_front();
        }

        _active_count++;
        MeshResult result = _process_job(job);
        _active_count--;
        _pending_count--;

        // Push result
        {
            std::lock_guard<std::mutex> lock(_result_mutex);
            _result_queue.push_back(std::move(result));
        }
    }
}

ChunkMeshWorker::MeshResult ChunkMeshWorker::_process_job(const MeshJob &job) {
    MeshResult result;
    result.cx = job.cx;
    result.cy = job.cy;
    result.cz = job.cz;
    result.lod_level = job.lod_level;
    result.empty = true;

    if (!_world || !_world->is_initialized()) {
        return result;
    }

    // Fast path: skip if this chunk and all face-neighbors are empty
    const VoxelChunk *center = _world->get_chunk(job.cx, job.cy, job.cz);
    if (center && center->is_empty()) {
        bool any_neighbor_solid = false;
        for (int dx = -1; dx <= 1 && !any_neighbor_solid; dx++)
            for (int dy = -1; dy <= 1 && !any_neighbor_solid; dy++)
                for (int dz = -1; dz <= 1 && !any_neighbor_solid; dz++) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    const VoxelChunk *n = _world->get_chunk(job.cx + dx, job.cy + dy, job.cz + dz);
                    if (n && !n->is_empty()) any_neighbor_solid = true;
                }
        if (!any_neighbor_solid) return result;
    }

    // Build padded voxel array with neighbor data (read-only access to chunks)
    const VoxelChunk *neighbors[3][3][3];
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dz = -1; dz <= 1; dz++) {
                neighbors[dx + 1][dy + 1][dz + 1] = _world->get_chunk(
                    job.cx + dx, job.cy + dy, job.cz + dz);
            }
        }
    }

    // Allocate padded buffers on stack (~38KB each)
    uint8_t padded[VoxelMesherBlocky::CS_P3];
    VoxelMesherBlocky::build_padded_voxels(neighbors, padded);

    if (job.lod_level > 0) {
        // Downsample for LOD
        uint8_t lod_padded[VoxelMesherBlocky::CS_P3];
        VoxelLOD::downsample_padded(padded, lod_padded, job.lod_level);
        result.mesh = VoxelMesherBlocky::mesh_chunk(lod_padded);
    } else {
        result.mesh = VoxelMesherBlocky::mesh_chunk(padded);
    }

    result.empty = result.mesh.empty;
    return result;
}

// ── GDScript Binding ─────────────────────────────────────────────────────

void ChunkMeshWorker::_bind_methods() {
    ClassDB::bind_method(D_METHOD("setup", "world", "num_threads"), &ChunkMeshWorker::setup, DEFVAL(4));
    ClassDB::bind_method(D_METHOD("queue_mesh", "cx", "cy", "cz", "lod_level"), &ChunkMeshWorker::queue_mesh, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("queue_mesh_batch", "coords", "camera_pos", "prioritize_near", "lod_level"),
                         &ChunkMeshWorker::queue_mesh_batch, DEFVAL(true), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("poll_results", "max_results"), &ChunkMeshWorker::poll_results, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("get_pending_count"), &ChunkMeshWorker::get_pending_count);
    ClassDB::bind_method(D_METHOD("get_completed_count"), &ChunkMeshWorker::get_completed_count);
    ClassDB::bind_method(D_METHOD("is_idle"), &ChunkMeshWorker::is_idle);
    ClassDB::bind_method(D_METHOD("shutdown"), &ChunkMeshWorker::shutdown);
}
