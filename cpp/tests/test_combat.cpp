// ═════════════════════════════════════════════════════════════
// Category 3: COMBAT — Fire Discipline & Targeting
// ═════════════════════════════════════════════════════════════

// Helper: read ammo from an entity (Flecs v4 returns const T by value)
static int get_ammo(flecs::entity e) { return e.get<MusketState>().ammo_count; }

TEST_CASE_FIXTURE(EngineTestHarness, "Cat3: HOLD prevents firing entirely") {
  // Need >10 in battalion to avoid shatter
  auto shooter = spawn_armed_soldier(0, 5.0f, 0.0f, 0);
  for (int i = 0; i < 19; i++)
    spawn_armed_soldier(0, (float)i * 0.8f, 0.0f, 0);
  // Add officer so discipline sticks
  spawn_soldier(0, 10.0f, 0.0f, 0).add<ElevatedLOS>();

  spawn_armed_soldier(1, 0.0f, -50.0f, 0);
  for (int i = 0; i < 19; i++)
    spawn_armed_soldier(1, (float)i * 0.8f, -50.0f, 0);

  g_macro_battalions[0].fire_discipline = DISCIPLINE_HOLD;
  step(60);

  CHECK(get_ammo(shooter) == 60);
}

TEST_CASE_FIXTURE(EngineTestHarness, "Cat3: AT_WILL allows firing") {
  auto shooter = spawn_armed_soldier(0, 0.0f, 0.0f, 0);
  for (int i = 0; i < 19; i++)
    spawn_armed_soldier(0, (float)i * 0.8f, 0.0f, 0);
  // Officer keeps discipline + extends range
  spawn_soldier(0, 10.0f, 0.0f, 0).add<ElevatedLOS>();

  // Enemies within musket range
  for (int i = 0; i < 20; i++)
    spawn_armed_soldier(1, (float)i * 0.8f, -50.0f, 0);

  g_macro_battalions[0].fire_discipline = DISCIPLINE_AT_WILL;

  step(600); // 10 seconds — plenty of time to reload and fire

  CHECK(get_ammo(shooter) < 60);
}

TEST_CASE_FIXTURE(EngineTestHarness,
                  "Cat3: Trap 27 - Routing soldiers NEVER fire") {
  auto shooter = spawn_armed_soldier(0, 0.0f, 0.0f, 0);
  shooter.add<Routing>();

  for (int i = 0; i < 19; i++)
    spawn_armed_soldier(0, (float)i * 0.8f, 0.0f, 0);

  for (int i = 0; i < 20; i++)
    spawn_armed_soldier(1, (float)i * 0.8f, -50.0f, 0);

  g_macro_battalions[0].fire_discipline = DISCIPLINE_AT_WILL;
  step(60);

  CHECK(get_ammo(shooter) == 60);
}

TEST_CASE_FIXTURE(EngineTestHarness, "Cat3: BY_RANK - only active rank fires") {
  auto rank0 = spawn_armed_soldier(0, 0.0f, 0.0f, 0);
  auto rank1 = spawn_armed_soldier(0, 0.8f, 0.0f, 1);
  auto rank2 = spawn_armed_soldier(0, 1.6f, 0.0f, 2);

  // Fill out battalion so >10 alive (avoid shatter)
  for (int i = 0; i < 17; i++)
    spawn_armed_soldier(0, (float)(i + 3) * 0.8f, 0.0f, 0);
  // Officer keeps BY_RANK from degrading
  spawn_soldier(0, 15.0f, 0.0f, 0).add<ElevatedLOS>();

  // Enemies
  for (int i = 0; i < 20; i++)
    spawn_armed_soldier(1, (float)i * 0.8f, -50.0f, 0);

  g_macro_battalions[0].fire_discipline = DISCIPLINE_BY_RANK;
  g_macro_battalions[0].active_firing_rank = 0;
  g_macro_battalions[0].volley_timer = 3.0f;

  step(120); // 2 seconds (before rank rotation at 3s)

  CHECK(get_ammo(rank0) < 60);  // Front rank fired
  CHECK(get_ammo(rank1) == 60); // Mid rank held
  CHECK(get_ammo(rank2) == 60); // Rear rank held
}

TEST_CASE_FIXTURE(EngineTestHarness, "Cat3: Dead officer forces AT_WILL") {
  for (int i = 0; i < 20; i++)
    spawn_soldier(0, (float)i, 0.0f, 0);

  // No officer entity → officer_alive = false after centroid pass
  g_macro_battalions[0].fire_discipline = DISCIPLINE_BY_RANK;
  step(1);

  CHECK(g_macro_battalions[0].fire_discipline == DISCIPLINE_AT_WILL);
}

TEST_CASE_FIXTURE(EngineTestHarness, "Cat3: can_shoot=false prevents firing") {
  auto shooter = spawn_armed_soldier(0, 0.0f, 0.0f, 0);
  shooter.ensure<SoldierFormationTarget>().can_shoot = false;

  for (int i = 0; i < 19; i++)
    spawn_armed_soldier(0, (float)i * 0.8f, 0.0f, 0);

  for (int i = 0; i < 20; i++)
    spawn_armed_soldier(1, (float)i * 0.8f, -50.0f, 0);

  g_macro_battalions[0].fire_discipline = DISCIPLINE_AT_WILL;
  step(60);

  CHECK(get_ammo(shooter) == 60);
}
