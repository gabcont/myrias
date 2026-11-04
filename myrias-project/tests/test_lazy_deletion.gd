extends Node2D

func _ready() -> void:
	print("--- Running Lazy Deletion Tests ---")
	await test_tail_trimming()
	await test_fragmentation_threshold_compaction()
	await test_pool_exhaustion_spawn_compaction()
	await test_spatial_grid_lazy_deletion()
	await test_edge_cases()
	await test_command_ordering_saturation()
	await test_bounds_stage()
	await test_animation_and_render_packet_updates()
	await test_soa_pooling_and_capacity()
	print("--- All Lazy Deletion Tests PASSED! ---")
	get_tree().quit(0)

func step_frame() -> void:
	await get_tree().physics_frame
	await get_tree().process_frame

func test_tail_trimming() -> void:
	print("[Test] Tail trimming...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	var gid := MyriasServer.create_group(model, 10)
	assert(gid >= 0, "Failed to create group")

	# Spawn 5 entities
	for i in range(5):
		MyriasServer.spawn(gid, Vector2(100 + i * 10, 100), Vector2.ZERO)

	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 5, "Expected 5 active entities")

	# Despawn tail entity (index 4)
	MyriasServer.despawn(gid, 4)
	# Also despawn index 3
	MyriasServer.despawn(gid, 3)

	# Tail trimming should reduce last_active from 5 to 3 immediately
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 3, "Expected 3 active entities after tail despawn")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Tail trimming test passed.")

func test_fragmentation_threshold_compaction() -> void:
	print("[Test] Fragmentation threshold compaction (>50% dead)...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	var gid := MyriasServer.create_group(model, 10)

	# Spawn 6 entities (indices 0..5)
	for i in range(6):
		MyriasServer.spawn(gid, Vector2(100 + i * 10, 100), Vector2.ZERO)

	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 6, "Expected 6 active entities")

	# Despawn 1 entity (index 2): dead = 1, last_active = 6. (6-5)*2 = 2 <= 6. No dirty.
	MyriasServer.despawn(gid, 2)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 5, "Expected 5 active entities")

	# Despawn 3 more non-tail entities (0, 1, 3):
	# active_amount becomes 2, last_active is still 6 (since index 5 and 4 are alive).
	# dead = 6 - 2 = 4. 4 * 2 = 8 > 6. Dirty flag is set!
	MyriasServer.despawn(gid, 0)
	MyriasServer.despawn(gid, 1)
	MyriasServer.despawn(gid, 3)

	# Next tick should execute compaction and clear dirty
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 2, "Expected 2 active entities after compaction")

	# Now slots 0 and 1 are the surviving entities (formerly 4 and 5)
	# Verify we can despawn them properly
	MyriasServer.despawn(gid, 1)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 1, "Expected 1 active entity")

	MyriasServer.despawn(gid, 0)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 0, "Expected 0 active entities")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Fragmentation threshold compaction test passed.")

func test_pool_exhaustion_spawn_compaction() -> void:
	print("[Test] Pool exhaustion spawn compaction...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	var pool_sz := 4
	var gid := MyriasServer.create_group(model, pool_sz)

	# Fill pool completely (indices 0, 1, 2, 3)
	for i in range(pool_sz):
		MyriasServer.spawn(gid, Vector2(50 + i * 10, 50), Vector2.ZERO)

	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 4, "Expected 4 active entities")

	# Despawn middle entity index 1:
	# active_amount = 3, last_active = 4 (tail index 3 still alive).
	# dead = 1. (4 - 3) * 2 = 2 <= 4. Not dirty!
	MyriasServer.despawn(gid, 1)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 3, "Expected 3 active entities")

	# Now spawn a 4th entity when last_active == pool_size (4).
	# exec_spawn must trigger compaction immediately to free up hole, then spawn.
	MyriasServer.spawn(gid, Vector2(500, 500), Vector2.ZERO)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 4, "Expected 4 active entities after filling hole")

	# Try to spawn when pool is truly full (active_amount == 4 == pool_size)
	MyriasServer.spawn(gid, Vector2(999, 999), Vector2.ZERO)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 4, "Spawn should be rejected when pool is fully packed")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Pool exhaustion spawn compaction test passed.")

func test_spatial_grid_lazy_deletion() -> void:
	print("[Test] Spatial grid with lazy deletion...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	model.set_radius(16.0)
	var gid := MyriasServer.create_group(model, 10)
	MyriasServer.configure_spatial_grid(Rect2(0, 0, 1000, 1000), 32.0)

	# Spawn two entities far apart
	MyriasServer.spawn(gid, Vector2(100, 100), Vector2.ZERO) # idx 0
	MyriasServer.spawn(gid, Vector2(500, 500), Vector2.ZERO) # idx 1

	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 2, "Expected 2 active entities")

	# Query collision at (100, 100) -> should collide
	var hit := MyriasServer.query_collision(Vector2(100, 100), 10.0, gid)
	assert(hit == true, "Expected collision at (100, 100)")

	# Despawn idx 0 (dead hole, last_active stays 2 because idx 1 is alive)
	MyriasServer.despawn(gid, 0)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 1, "Expected 1 active entity")

	# Query collision at (100, 100) -> should NOT collide because entity 0 is dead!
	hit = MyriasServer.query_collision(Vector2(100, 100), 10.0, gid)
	assert(hit == false, "Dead entity at (100, 100) must not register collision")

	# Query collision at (500, 500) -> should collide with alive entity 1
	hit = MyriasServer.query_collision(Vector2(500, 500), 10.0, gid)
	assert(hit == true, "Alive entity at (500, 500) must register collision")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Spatial grid with lazy deletion test passed.")


func test_edge_cases() -> void:
	print("[Test] Edge cases (OOB, duplicate despawn, full pool recycling, physics events)...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	var gid := MyriasServer.create_group(model, 5)

	# 1. Despawn on empty group
	MyriasServer.despawn(gid, 0)
	MyriasServer.despawn(gid, -1)
	MyriasServer.despawn(gid, 100)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 0, "Count should remain 0")

	# 2. Duplicate despawn
	MyriasServer.spawn(gid, Vector2(10, 10), Vector2.ZERO)
	MyriasServer.spawn(gid, Vector2(20, 20), Vector2.ZERO)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 2)
	MyriasServer.despawn(gid, 0)
	MyriasServer.despawn(gid, 0)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 1, "Duplicate despawn must not double decrement")

	# 3. OOB despawn with active entities
	MyriasServer.despawn(gid, -1)
	MyriasServer.despawn(gid, 2)
	MyriasServer.despawn(gid, 999)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 1, "OOB despawn must be no-op")

	# 4. Despawn remaining tail entity
	MyriasServer.despawn(gid, 1)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 0, "Count must be 0")

	# 5. Spawning immediately after clearing
	for i in range(5):
		MyriasServer.spawn(gid, Vector2(100 + i * 10, 100 + i * 10), Vector2.ZERO)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 5, "Pool full again")

	# 6. Despawn all entities head to tail (0, then 1, 2, 3, 4)
	for i in range(5):
		MyriasServer.despawn(gid, i)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 0, "All despawned head-to-tail")

	# 7. Spawning again to pool full, then despawn tail to head (4, 3, 2, 1, 0)
	for i in range(5):
		MyriasServer.spawn(gid, Vector2(100 + i * 10, 100 + i * 10), Vector2.ZERO)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 5)
	for i in range(4, -1, -1):
		MyriasServer.despawn(gid, i)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 0, "All despawned tail-to-head")

	# 8. Physics event despawn
	MyriasServer.spawn(gid, Vector2(10, 10), Vector2.ZERO)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 1)
	MyriasServer.emit_despawn_event(gid, 0)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 0, "Despawn via physics event")

	# 9. Physics event duplicate despawn / OOB
	MyriasServer.emit_despawn_event(gid, 0)
	MyriasServer.emit_despawn_event(gid, 99)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 0, "Event OOB must be no-op")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Edge cases test passed.")

func test_command_ordering_saturation() -> void:
	print("[Test] Command ordering (DESPAWN before SPAWN-triggered compaction in same batch)...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	model.set_radius(16.0)
	var pool_sz := 4
	var gid := MyriasServer.create_group(model, pool_sz)
	MyriasServer.configure_spatial_grid(Rect2(0, 0, 1000, 1000), 32.0)

	# Step 1: Fill pool with 4 entities at distinct positions
	MyriasServer.spawn(gid, Vector2(100, 100), Vector2.ZERO) # idx 0
	MyriasServer.spawn(gid, Vector2(200, 200), Vector2.ZERO) # idx 1
	MyriasServer.spawn(gid, Vector2(300, 300), Vector2.ZERO) # idx 2
	MyriasServer.spawn(gid, Vector2(400, 400), Vector2.ZERO) # idx 3
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 4, "Expected 4 active entities")

	# Step 2: In the SAME batch, queue a despawn of entity 1, followed by a new spawn
	# With phased command execution, DESPAWN runs before SPAWN.
	# The despawn clears entity 1, reducing active count to 3.
	# Then the spawn sees pool full (last_active == 4), triggers compaction to recycle slot 1,
	# and successfully spawns the new entity at (500, 500).
	MyriasServer.despawn(gid, 1)
	MyriasServer.spawn(gid, Vector2(500, 500), Vector2.ZERO)

	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 4, "Expected 4 active entities after recycling")

	# Verify: entity at (200, 200) was destroyed
	assert(MyriasServer.query_collision(Vector2(200, 200), 10.0, gid) == false, "Despawned entity 1 must not exist")
	# Verify: entities 0, 2, 3 survived compaction
	assert(MyriasServer.query_collision(Vector2(100, 100), 10.0, gid) == true, "Entity 0 must exist")
	assert(MyriasServer.query_collision(Vector2(300, 300), 10.0, gid) == true, "Entity 2 must exist")
	assert(MyriasServer.query_collision(Vector2(400, 400), 10.0, gid) == true, "Entity 3 must exist")
	# Verify: new spawned entity exists
	assert(MyriasServer.query_collision(Vector2(500, 500), 10.0, gid) == true, "New entity at 500, 500 must exist")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Command ordering saturation test passed.")

func test_bounds_stage() -> void:
	print("[Test] Bounds stage: X bounce and Y kill...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	model.set_radius(16.0)
	var gid := MyriasServer.create_group(model, 10)
	MyriasServer.configure_spatial_grid(Rect2(0, 0, 2000, 2000), 32.0)

	# 1. Test X bounce: moving left towards <= 0
	# pos = (5, 100), vel = (-100, 0). After delta, pos becomes < 0, bounces to vel_x = +100
	MyriasServer.spawn(gid, Vector2(5, 100), Vector2(-100, 0))
	await step_frame() # tick integration + bounds bounce
	await step_frame() # tick again moving right
	# Entity should still be alive and have moved right (x > 0)
	assert(MyriasServer.get_active_count(gid) == 1, "Bounced entity should still be alive")

	# 2. Test Y kill: moving down past >= 720
	# pos = (500, 715), vel = (0, 500). After delta, y >= 720, should be killed
	MyriasServer.spawn(gid, Vector2(500, 715), Vector2(0, 500))
	await step_frame()
	await step_frame()
	# The Y entity should have been killed, leaving only the X entity
	assert(MyriasServer.get_active_count(gid) == 1, "Y out-of-bounds entity must be killed")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Bounds stage test passed.")

func test_animation_and_render_packet_updates() -> void:
	print("[Test] Animation advancement and render packet updates...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	var gid := MyriasServer.create_group(model, 16)
	
	# Spawn with rotation and scale
	MyriasServer.spawn(gid, Vector2(100, 100), Vector2.ZERO, 0, 2.0, 1.57)
	MyriasServer.spawn(gid, Vector2(200, 200), Vector2.ZERO, 0, 1.0, 0.0)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 2, "2 active entities")
	
	# Let animation frames advance across multiple ticks
	for i in range(15):
		await step_frame()
	
	# Emit animation change event
	MyriasServer.emit_animation_change_event(gid, 0, 0)
	await step_frame()
	assert(MyriasServer.get_active_count(gid) == 2, "Active count preserved after animation change")
	
	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Animation advancement and render packet updates passed.")

func test_soa_pooling_and_capacity() -> void:
	print("[Test] SoA contiguous pooling and capacity management...")
	var initial_cap := MyriasServer.get_soa_capacity()
	assert(initial_cap == 120000, "Default SoA capacity must be 120000, got: %d" % initial_cap)
	assert(MyriasServer.get_soa_used() == 0, "Initial SoA used must be 0")

	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")

	# Create group 1 with pool_size 100
	var gid1 := MyriasServer.create_group(model, 100)
	await step_frame()
	assert(MyriasServer.get_soa_used() == 100, "SoA used must be 100, got: %d" % MyriasServer.get_soa_used())

	# Create group 2 with pool_size 200
	var gid2 := MyriasServer.create_group(model, 200)
	await step_frame()
	assert(MyriasServer.get_soa_used() == 300, "SoA used must be 300, got: %d" % MyriasServer.get_soa_used())

	# Attempt to shrink capacity below used (e.g. 250 < 300) -> should fail
	var ok := MyriasServer.set_soa_capacity(250)
	assert(ok == false, "set_soa_capacity below used must return false")
	assert(MyriasServer.get_soa_capacity() == 120000, "Capacity must remain 120000 after failed resize")

	# Expand capacity to 130000 -> should succeed
	ok = MyriasServer.set_soa_capacity(130000)
	assert(ok == true, "set_soa_capacity to 130000 must succeed")
	assert(MyriasServer.get_soa_capacity() == 130000, "Capacity must now be 130000")

	# Spawn entities in both groups
	MyriasServer.spawn(gid1, Vector2(100, 100), Vector2(10, 0), 0, 1.5, 0.5)
	MyriasServer.spawn(gid2, Vector2(200, 200), Vector2(0, 20), 0, 2.0, 1.0)
	await step_frame()

	assert(is_equal_approx(MyriasServer.get_entity_scale(gid1, 0), 1.5), "gid1 entity scale must be 1.5")
	assert(is_equal_approx(MyriasServer.get_entity_rotation(gid1, 0), 0.5), "gid1 entity rotation must be 0.5")
	assert(is_equal_approx(MyriasServer.get_entity_scale(gid2, 0), 2.0), "gid2 entity scale must be 2.0")
	assert(is_equal_approx(MyriasServer.get_entity_rotation(gid2, 0), 1.0), "gid2 entity rotation must be 1.0")

	# Remove group 1: group 2 shifts left in SoA
	MyriasServer.remove_group(gid1)
	await step_frame()

	assert(MyriasServer.get_soa_used() == 200, "SoA used must be 200 after removing gid1, got: %d" % MyriasServer.get_soa_used())
	# Entity in gid2 must still exist and have correct values
	assert(is_equal_approx(MyriasServer.get_entity_scale(gid2, 0), 2.0), "gid2 entity scale must remain 2.0 after slice shift")
	assert(is_equal_approx(MyriasServer.get_entity_rotation(gid2, 0), 1.0), "gid2 entity rotation must remain 1.0 after slice shift")

	# Remove group 2
	MyriasServer.remove_group(gid2)
	await step_frame()
	assert(MyriasServer.get_soa_used() == 0, "SoA used must be 0 after removing all groups")

	# Restore default capacity
	ok = MyriasServer.set_soa_capacity(120000)
	assert(ok == true, "Restoring capacity to 120000 must succeed")
	assert(MyriasServer.get_soa_capacity() == 120000, "Capacity restored to 120000")
	print("  -> SoA contiguous pooling and capacity management passed.")
