#ifndef CHUNK_MESH_WORKER_H
#define CHUNK_MESH_WORKER_H

#include "voxel_mesher_blocky.h"
#include "voxel_lod.h"
#include "voxel_world.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <deque>
#include <atomic>

namespace godot {

/// Thread-pool mesher for parallel chunk meshing.
///
/// Worker threads read voxel data (read-only during meshing), build padded
/// voxels, and run the binary greedy mesher. Results are queued for the
/// main thread to upload to RenderingServer.
///
/// Usage from GDScript:
///   var worker = ChunkMeshWorker.new()
///   worker.setup(world, 4)
///   worker.queue_mesh(cx, cy, cz)
///   ...
///   var results = worker.poll_results(16)
///   for r in results:
///       upload_to_rendering_server(r)
///   ...
///   worker.shutdown()
class ChunkMeshWorker : public RefCounted {
    GDCLASS(ChunkMeshWorker, RefCounted)

public:
    ChunkMeshWorker();
    ~ChunkMeshWorker();

    /// Initialize the thread pool.
    /// world: VoxelWorld node (must remain valid while worker is active)
    /// num_threads: number of worker threads (default 4)
    void setup(VoxelWorld *world, int num_threads = 4);

    /// Queue a chunk for meshing at a specific LOD level. Thread-safe.
    /// lod_level: 0=full, 1=half (16^3), 2=quarter (8^3)
    void queue_mesh(int cx, int cy, int cz, int lod_level = 0);

    /// Queue multiple chunks. coords is flat [cx0,cy0,cz0, cx1,cy1,cz1, ...].
    /// If prioritize_near is true, sorts by distance to camera_pos before queuing.
    void queue_mesh_batch(const PackedInt32Array &coords, const Vector3 &camera_pos, bool prioritize_near, int lod_level = 0);

    /// Poll completed mesh results (non-blocking).
    /// Returns Array of Dictionaries: {"cx": int, "cy": int, "cz": int, "arrays": Array, "empty": bool}
    /// max_results: maximum number of results to dequeue (0 = all available)
    Array poll_results(int max_results = 0);

    /// Number of jobs pending in the queue.
    int get_pending_count() const;

    /// Number of results ready to be polled.
    int get_completed_count() const;

    /// True if all queued work is done and results are available.
    bool is_idle() const;

    /// Shut down all worker threads. Blocks until threads join.
    void shutdown();

protected:
    static void _bind_methods();

private:
    /// A mesh job: chunk coordinates + LOD level.
    struct MeshJob {
        int cx, cy, cz;
        int lod_level;  // 0=full, 1=half, 2=quarter
    };

    /// A completed mesh result.
    struct MeshResult {
        int cx, cy, cz;
        int lod_level;
        VoxelMesherBlocky::ChunkMesh mesh;
        bool empty;
    };

    /// Worker thread function.
    void _worker_func();

    /// Process a single mesh job (runs on worker thread).
    MeshResult _process_job(const MeshJob &job);

    VoxelWorld *_world = nullptr;
    std::vector<std::thread> _threads;
    bool _running = false;

    // Job queue (producer: main thread, consumer: workers)
    std::deque<MeshJob> _job_queue;
    mutable std::mutex _job_mutex;
    std::condition_variable _job_cv;

    // Result queue (producer: workers, consumer: main thread)
    std::deque<MeshResult> _result_queue;
    mutable std::mutex _result_mutex;

    // Stats
    std::atomic<int> _pending_count{0};
    std::atomic<int> _active_count{0};
};

} // namespace godot

#endif // CHUNK_MESH_WORKER_H
