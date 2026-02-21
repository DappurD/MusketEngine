// ═════════════════════════════════════════════════════════════
// Category 6: PERFORMANCE — Regression Bounds
// ═════════════════════════════════════════════════════════════
#include <chrono>

TEST_CASE_FIXTURE(EngineTestHarness, "Cat6: 1K entities tick under 50ms") {
  // NOTE: VolleyFireSystem does O(N) w.each() per soldier for micro-targeting,
  // making overall combat tick O(N²). With 1K entities across 10 battalions,
  // this is a realistic battlefield scenario that must stay under budget.
  for (int i = 0; i < 1000; i++) {
    uint32_t bat = (uint32_t)(i / 100);
    float x = (float)(i % 100) * 0.8f;
    float z = (float)(bat % 2) * 100.0f;
    spawn_armed_soldier(bat, x, z, (uint8_t)(bat % 2));
  }

  // Warmup — build Flecs archetypes and CPU caches
  step(1);

  // Measure a single 60Hz frame
  auto start = std::chrono::high_resolution_clock::now();
  step(1);
  auto end = std::chrono::high_resolution_clock::now();

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                .count();

  MESSAGE("1K entity tick: ", ms, "ms");
  CHECK(ms < 50);
}

TEST_CASE_FIXTURE(EngineTestHarness, "Cat6: Spawn 5K entities under 50ms") {
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < 5000; i++) {
    spawn_armed_soldier((uint32_t)(i / 200), (float)(i % 200) * 0.8f, 0.0f,
                        (uint8_t)(i % 2));
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                .count();

  MESSAGE("5K spawn time: ", ms, "ms");
  CHECK(ms < 50);
}
