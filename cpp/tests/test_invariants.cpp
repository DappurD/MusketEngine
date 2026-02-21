// ═════════════════════════════════════════════════════════════
// Category 1: INVARIANTS — must ALWAYS hold
// ═════════════════════════════════════════════════════════════

TEST_CASE("Cat1: Component Memory Layout") {
  SUBCASE("SoldierFormationTarget is exactly 1 cache line") {
    CHECK(sizeof(SoldierFormationTarget) == 64);
    CHECK(alignof(SoldierFormationTarget) == 64);
  }

  SUBCASE("MacroBattalion fits in 2 cache lines") {
    CHECK(sizeof(MacroBattalion) <= 128);
  }
}

TEST_CASE_FIXTURE(EngineTestHarness,
                  "Cat1: Trap 23 - Centroid preserves persistent data") {
  // Set persistent fields to non-default values
  g_macro_battalions[0].flag_cohesion = 0.5f;
  g_macro_battalions[0].dir_x = 0.8f;
  g_macro_battalions[0].fire_discipline = DISCIPLINE_BY_RANK;
  g_macro_battalions[0].volley_timer = 2.5f;

  // Spawn >10 soldiers so shatter check doesn't wipe command tags
  for (int i = 0; i < 20; i++)
    spawn_soldier(0, (float)i, 0.0f, 0);

  // Add a flag bearer + officer so they stay alive through centroid
  spawn_soldier(0, 10.0f, 0.0f, 0).add<FormationAnchor>();
  spawn_soldier(0, 11.0f, 0.0f, 0).add<ElevatedLOS>();

  step(1);

  // Persistent data survived the centroid pass!
  CHECK(g_macro_battalions[0].dir_x == doctest::Approx(0.8f));
  // Cohesion should have increased slightly (flag alive)
  CHECK(g_macro_battalions[0].flag_cohesion >= 0.5f);
  CHECK(g_macro_battalions[0].flag_cohesion <= 1.0f);
  // Fire discipline preserved (officer alive → no forced AT_WILL)
  CHECK(g_macro_battalions[0].fire_discipline == DISCIPLINE_BY_RANK);
}

TEST_CASE_FIXTURE(EngineTestHarness,
                  "Cat1: Cohesion bounds - floor 0.2, cap 1.0") {
  // Spawn a full battalion (no flag) so it stays above shatter threshold
  for (int i = 0; i < 50; i++)
    spawn_soldier(0, (float)i * 0.8f, 0.0f, 0);

  // Run many frames — cohesion should decay to floor but never below
  step(60 * 50); // ~50 seconds at 60Hz

  CHECK(g_macro_battalions[0].flag_cohesion ==
        doctest::Approx(0.2f).epsilon(0.01));
}

TEST_CASE_FIXTURE(
    EngineTestHarness,
    "Cat1: Trap 32 - Stale target_bat_id resets when enemy dies") {
  // Spawn healthy blue battalion
  for (int i = 0; i < 30; i++) {
    spawn_soldier(0, (float)i * 0.8f, 0.0f, 0);
    spawn_soldier(1, (float)i * 0.8f, -50.0f, 1);
  }

  step(1);
  CHECK(g_macro_battalions[0].target_bat_id == 1); // Targeting red

  // Kill all red soldiers using deferred operations (Flecs One Frame Rule)
  ecs.defer_begin();
  ecs.each([](flecs::entity e, const TeamId &t) {
    if (t.team == 1)
      e.remove<IsAlive>();
  });
  ecs.defer_end();

  step(1);
  CHECK(g_macro_battalions[0].target_bat_id == -1); // No target!
}

TEST_CASE_FIXTURE(EngineTestHarness,
                  "Cat1: Dead entities don't accumulate in centroid") {
  auto e1 = spawn_soldier(0, 100.0f, 0.0f, 0);
  auto e2 = spawn_soldier(0, 0.0f, 0.0f, 0);
  // Need >10 total to avoid shatter, add more
  for (int i = 0; i < 12; i++)
    spawn_soldier(0, 50.0f, 0.0f, 0);

  step(1);
  CHECK(g_macro_battalions[0].alive_count == 14);

  // Kill entity at 100.0
  ecs.defer_begin();
  e1.remove<IsAlive>();
  ecs.defer_end();
  step(1);

  CHECK(g_macro_battalions[0].alive_count == 13);
  // Centroid should shift toward 0.0 (remaining soldiers mostly at 0 and 50)
  CHECK(g_macro_battalions[0].cx < 100.0f);
}
