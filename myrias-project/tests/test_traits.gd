extends Node2D

func _ready() -> void:
	print("--- Running Myrias Trait System Tests ---")
	await test_follow_steering_and_angular()
	await test_sine_wave()
	await test_curve_modifier()
	await test_face_velocity_and_face_node_nan_safety()
	await test_per_entity_trait_gain_loss_and_compaction()
	await test_myrias_group_resource_array_integration()
	print("--- All Myrias Trait System Tests PASSED! ---")
	get_tree().quit(0)

func step_frame() -> void:
	await get_tree().physics_frame
	await get_tree().process_frame

func test_follow_steering_and_angular() -> void:
	print("[Test 1] Follow Steering and Follow Angular...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")

	# Part A: Follow Steering
	var gid_steer := MyriasServer.create_group(model, 16)
	assert(gid_steer >= 0, "Failed to create steering group")

	# Target at (300, 100). Spawn at (100, 100) moving purely along Y (0, 100).
	# Direction to target is +X. Steering force will add +X acceleration.
	var steer := MyriasTraitFollowSteering.new()
	steer.target_pos = Vector2(300, 100)
	steer.turn_force = 500.0
	steer.max_speed = 400.0
	MyriasServer.configure_trait(gid_steer, steer)
	MyriasServer.spawn(gid_steer, Vector2(100, 100), Vector2(0, 100))
	await step_frame()

	var vel_after_1 := MyriasServer.get_entity_velocity(gid_steer, 0)
	assert(vel_after_1.x > 0.0, "Steering should accelerate entity toward target along +X")

	# Test max_speed clamping: set max_speed to 80.0
	steer.turn_force = 1000.0
	steer.max_speed = 80.0
	MyriasServer.configure_trait(gid_steer, steer)
	for i in range(5):
		await step_frame()

	var vel_clamped := MyriasServer.get_entity_velocity(gid_steer, 0)
	assert(vel_clamped.length() <= 80.01, "Velocity should be clamped to max_speed (80.0)")

	MyriasServer.remove_group(gid_steer)
	await step_frame()

	# Part B: Follow Angular
	var gid_ang := MyriasServer.create_group(model, 16)
	assert(gid_ang >= 0, "Failed to create angular group")

	# Target at (300, 100). Spawn at (100, 100) moving at (0, 100). Speed is 100.0.
	var ang := MyriasTraitFollowAngular.new()
	ang.target_pos = Vector2(300, 100)
	ang.turn_rate = 2.0
	MyriasServer.configure_trait(gid_ang, ang)
	MyriasServer.spawn(gid_ang, Vector2(100, 100), Vector2(0, 100))
	await step_frame()

	var vel_ang := MyriasServer.get_entity_velocity(gid_ang, 0)
	assert(vel_ang.x > 0.0, "Angular homing should turn velocity vector toward target (+X)")
	assert(is_equal_approx(vel_ang.length(), 100.0), "Angular homing MUST strictly preserve scalar speed")

	MyriasServer.remove_group(gid_ang)
	await step_frame()
	print("  -> Follow Steering and Angular passed.")

func test_sine_wave() -> void:
	print("[Test 2] Sine Wave Movement...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	var gid := MyriasServer.create_group(model, 16)

	# Entity moving along +X at 100 px/s
	var sine := MyriasTraitSineWave.new()
	sine.amplitude = 40.0
	sine.frequency = 2.0
	sine.phase = 0.0
	MyriasServer.configure_trait(gid, sine)
	MyriasServer.spawn(gid, Vector2(100, 100), Vector2(100, 0))
	await step_frame()

	# Run for several frames
	var observed_lateral_movement := false
	for i in range(10):
		await step_frame()
		var pos := MyriasServer.get_entity_position(gid, 0)
		if abs(pos.y - 100.0) > 0.05:
			observed_lateral_movement = true

	assert(observed_lateral_movement, "Sine wave must induce lateral oscillation perpendicular to forward velocity")

	# Base forward velocity should be uncorrupted
	var vel := MyriasServer.get_entity_velocity(gid, 0)
	assert(is_equal_approx(vel.x, 100.0), "Base forward velocity X must remain 100.0")
	assert(is_equal_approx(vel.y, 0.0), "Base forward velocity Y must remain 0.0")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Sine Wave test passed.")

func test_curve_modifier() -> void:
	print("[Test 3] Curve Modifier (Scale, Rotation, Speed)...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")

	# Part A: Scale Modifier (curve from 1.0 to 4.0)
	var curve_scale := Curve.new()
	curve_scale.max_value = 4.0
	curve_scale.add_point(Vector2(0.0, 1.0))
	curve_scale.add_point(Vector2(1.0, 4.0))

	var gid_scale := MyriasServer.create_group(model, 16)
	var mod_scale := MyriasTraitCurveModifier.new()
	mod_scale.curve = curve_scale
	mod_scale.target_property = MyriasServer.CURVE_TARGET_SCALE
	mod_scale.duration = 1.0
	mod_scale.multiplier = 1.0
	mod_scale.loop = false
	MyriasServer.configure_trait(gid_scale, mod_scale)
	MyriasServer.spawn(gid_scale, Vector2(100, 100), Vector2(0, 0), 0, 1.0, 0.0)
	await step_frame()

	for i in range(5):
		await step_frame()

	var scale_val := MyriasServer.get_entity_scale(gid_scale, 0)
	assert(scale_val > 1.0, "Scale should increase along curve from 1.0 toward 4.0")
	MyriasServer.remove_group(gid_scale)
	await step_frame()

	# Part B: Rotation Modifier
	var curve_rot := Curve.new()
	curve_rot.add_point(Vector2(0.0, 0.0))
	curve_rot.add_point(Vector2(1.0, 3.14))

	var gid_rot := MyriasServer.create_group(model, 16)
	var mod_rot := MyriasTraitCurveModifier.new()
	mod_rot.curve = curve_rot
	mod_rot.target_property = MyriasServer.CURVE_TARGET_ROTATION
	mod_rot.duration = 1.0
	mod_rot.multiplier = 1.0
	mod_rot.loop = false
	MyriasServer.configure_trait(gid_rot, mod_rot)
	MyriasServer.spawn(gid_rot, Vector2(100, 100), Vector2(0, 0), 0, 1.0, 0.0)
	await step_frame()

	for i in range(5):
		await step_frame()

	var rot_val := MyriasServer.get_entity_rotation(gid_rot, 0)
	assert(rot_val > 0.0, "Rotation should increase along curve")
	MyriasServer.remove_group(gid_rot)
	await step_frame()

	# Part C: Speed Modifier (curve modulates target speed from 20.0 to 80.0 using multiplier 80.0)
	var curve_spd := Curve.new()
	curve_spd.add_point(Vector2(0.0, 0.25))
	curve_spd.add_point(Vector2(1.0, 1.0))

	var gid_speed := MyriasServer.create_group(model, 16)
	var mod_spd := MyriasTraitCurveModifier.new()
	mod_spd.curve = curve_spd
	mod_spd.target_property = MyriasServer.CURVE_TARGET_SPEED
	mod_spd.duration = 1.0
	mod_spd.multiplier = 80.0
	mod_spd.loop = false
	MyriasServer.configure_trait(gid_speed, mod_spd)
	MyriasServer.spawn(gid_speed, Vector2(100, 100), Vector2(10, 0), 0, 1.0, 0.0)
	await step_frame()

	for i in range(5):
		await step_frame()

	var vel_spd := MyriasServer.get_entity_velocity(gid_speed, 0)
	assert(vel_spd.length() > 20.0, "Velocity magnitude should be scaled along curve toward 80.0")
	assert(vel_spd.y == 0.0, "Velocity heading must be preserved along +X")
	MyriasServer.remove_group(gid_speed)
	await step_frame()
	print("  -> Curve Modifier test passed.")

func test_face_velocity_and_face_node_nan_safety() -> void:
	print("[Test 4] Face Velocity and Face Node NaN Safety...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")

	# Part A: Face Velocity
	var gid_vel := MyriasServer.create_group(model, 16)
	var fvel := MyriasTraitFaceVelocity.new()
	fvel.angle_offset = 0.0
	MyriasServer.configure_trait(gid_vel, fvel)
	# Entity 0: Zero velocity
	MyriasServer.spawn(gid_vel, Vector2(100, 100), Vector2.ZERO)
	# Entity 1: Velocity along +X (angle 0)
	MyriasServer.spawn(gid_vel, Vector2(100, 100), Vector2(100, 0))
	# Entity 2: Velocity along +Y (angle PI/2)
	MyriasServer.spawn(gid_vel, Vector2(100, 100), Vector2(0, 100))
	await step_frame()

	var rot0 := MyriasServer.get_entity_rotation(gid_vel, 0)
	assert(!is_nan(rot0), "Zero-velocity entity must NOT have NaN rotation")

	var rot1 := MyriasServer.get_entity_rotation(gid_vel, 1)
	assert(is_equal_approx(rot1, 0.0), "Entity moving +X must face 0 radians")

	var rot2 := MyriasServer.get_entity_rotation(gid_vel, 2)
	assert(is_equal_approx(rot2, PI / 2.0), "Entity moving +Y must face PI/2 radians")

	MyriasServer.remove_group(gid_vel)
	await step_frame()

	# Part B: Face Node
	var gid_node := MyriasServer.create_group(model, 16)
	var fnode := MyriasTraitFaceNode.new()
	fnode.target_pos = Vector2(100, 200)
	fnode.angle_offset = 0.0
	MyriasServer.configure_trait(gid_node, fnode)
	# Entity 0: At exact same position as target (distance = 0)
	MyriasServer.spawn(gid_node, Vector2(100, 200), Vector2.ZERO)
	# Entity 1: Directly above target at (100, 100), target is at (100, 200), so angle is PI/2 (+Y)
	MyriasServer.spawn(gid_node, Vector2(100, 100), Vector2.ZERO)
	await step_frame()

	var n_rot0 := MyriasServer.get_entity_rotation(gid_node, 0)
	assert(!is_nan(n_rot0), "Coincident-position entity must NOT have NaN rotation")

	var n_rot1 := MyriasServer.get_entity_rotation(gid_node, 1)
	assert(is_equal_approx(n_rot1, PI / 2.0), "Entity facing target directly below must have rot PI/2")

	MyriasServer.remove_group(gid_node)
	await step_frame()
	print("  -> Face Velocity and Face Node NaN Safety passed.")

func test_per_entity_trait_gain_loss_and_compaction() -> void:
	print("[Test 5] Dynamic Per-Entity Trait Gain/Loss + Compaction...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	var gid := MyriasServer.create_group(model, 16)

	# Configure sine wave as group trait
	var sine := MyriasTraitSineWave.new()
	sine.amplitude = 30.0
	sine.frequency = 2.0
	sine.phase = 0.0
	MyriasServer.configure_trait(gid, sine)

	# Spawn 7 entities (indices 0..6)
	for i in range(7):
		MyriasServer.spawn(gid, Vector2(100 + i * 20, 100), Vector2(100, 0))
	await step_frame()

	assert(MyriasServer.get_active_count(gid) == 7, "Expected 7 active entities")
	for i in range(7):
		assert(MyriasServer.has_entity_trait(gid, i, MyriasServer.TRAIT_SINE_WAVE), "Initial entity %d must have uniform sine wave" % i)

	# Dynamically disable sine wave on entities 1 and 3
	# Trait state: [0:true, 1:false, 2:true, 3:false, 4:true, 5:true, 6:true]
	MyriasServer.set_entity_trait(gid, 1, MyriasServer.TRAIT_SINE_WAVE, false)
	MyriasServer.set_entity_trait(gid, 3, MyriasServer.TRAIT_SINE_WAVE, false)
	await step_frame()

	assert(MyriasServer.has_entity_trait(gid, 0, MyriasServer.TRAIT_SINE_WAVE) == true, "Entity 0 enabled")
	assert(MyriasServer.has_entity_trait(gid, 1, MyriasServer.TRAIT_SINE_WAVE) == false, "Entity 1 disabled")
	assert(MyriasServer.has_entity_trait(gid, 2, MyriasServer.TRAIT_SINE_WAVE) == true, "Entity 2 enabled")
	assert(MyriasServer.has_entity_trait(gid, 3, MyriasServer.TRAIT_SINE_WAVE) == false, "Entity 3 disabled")
	assert(MyriasServer.has_entity_trait(gid, 4, MyriasServer.TRAIT_SINE_WAVE) == true, "Entity 4 enabled")
	assert(MyriasServer.has_entity_trait(gid, 5, MyriasServer.TRAIT_SINE_WAVE) == true, "Entity 5 enabled")
	assert(MyriasServer.has_entity_trait(gid, 6, MyriasServer.TRAIT_SINE_WAVE) == true, "Entity 6 enabled")

	# Despawn entities 0, 2, 4, 5 (4 out of 7 dead: (7 - 3) * 2 = 8 > 7, triggering fragmentation compaction)
	# Surviving entities:
	# old index 1 (disabled) -> moves to index 0
	# old index 3 (disabled) -> moves to index 1
	# old index 6 (enabled)  -> moves to index 2
	MyriasServer.despawn(gid, 0)
	MyriasServer.despawn(gid, 2)
	MyriasServer.despawn(gid, 4)
	MyriasServer.despawn(gid, 5)
	await step_frame()

	assert(MyriasServer.get_active_count(gid) == 3, "Expected 3 active entities after compaction")
	assert(MyriasServer.has_entity_trait(gid, 0, MyriasServer.TRAIT_SINE_WAVE) == false, "Compacted entity 0 (old 1) must be disabled")
	assert(MyriasServer.has_entity_trait(gid, 1, MyriasServer.TRAIT_SINE_WAVE) == false, "Compacted entity 1 (old 3) must be disabled")
	assert(MyriasServer.has_entity_trait(gid, 2, MyriasServer.TRAIT_SINE_WAVE) == true, "Compacted entity 2 (old 6) must remain enabled")

	# Edge case safety checks: zero trait flag, multi-bit/invalid flags, and null trait config
	assert(MyriasServer.has_entity_trait(gid, 0, 0) == false, "Trait flag 0 query must safely return false")
	assert(MyriasServer.has_entity_trait(gid, 0, 0xFFFFFFFF) == false, "Multi-bit flag query must safely return false")
	MyriasServer.set_entity_trait(gid, 0, 0, true)
	MyriasServer.set_entity_trait(gid, 0, 0xFFFFFFFF, true)
	MyriasServer.remove_group_trait(gid, 0)
	MyriasServer.remove_group_trait(gid, 0xFFFFFFFF)
	MyriasServer.configure_trait(gid, null)
	await step_frame()
	assert(MyriasServer.has_entity_trait(gid, 2, MyriasServer.TRAIT_SINE_WAVE) == true, "Entity 2 still retains valid traits after no-op invalid calls")

	MyriasServer.remove_group(gid)
	await step_frame()
	print("  -> Dynamic Per-Entity Trait Gain/Loss + Compaction passed.")

func test_myrias_group_resource_array_integration() -> void:
	print("[Test 6] MyriasGroup Resource Array Integration...")
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")

	# Create target node in tree
	var target_node := Node2D.new()
	target_node.name = "TestTarget"
	target_node.global_position = Vector2(400, 100)
	add_child(target_node)

	# Create MyriasGroup node
	var group_node := MyriasGroup.new()
	group_node.name = "TestGroup"
	group_node.model = model
	group_node.pool_size = 16

	# Create trait resource referencing the target node
	var steer_trait := MyriasTraitFollowSteering.new()
	steer_trait.target_node_path = NodePath("../TestTarget")
	steer_trait.turn_force = 400.0
	steer_trait.max_speed = 500.0

	var trait_list : Array[MyriasTrait] = [steer_trait]
	group_node.traits = trait_list

	add_child(group_node)
	await step_frame()

	var gid := group_node.get_group_id()
	assert(gid >= 0, "Group ID must be valid")

	# Spawn an entity into this group
	MyriasServer.spawn(gid, Vector2(100, 100), Vector2(0, 100))
	await step_frame()

	var vel := MyriasServer.get_entity_velocity(gid, 0)
	assert(vel.x > 0.0, "Entity spawned from MyriasGroup must follow configured steering toward TestTarget (+X)")

	# Dynamically modify trait resource property and verify hot-reloading via changed signal
	steer_trait.turn_force = 1200.0
	await step_frame()

	# Move target to opposite side (-400, 100)
	target_node.global_position = Vector2(-400, 100)
	for i in range(10):
		await step_frame()

	var vel_after_move := MyriasServer.get_entity_velocity(gid, 0)
	assert(vel_after_move.x < vel.x, "Entity should have steered towards the updated target position on the left")

	group_node.queue_free()
	target_node.queue_free()
	await step_frame()
	print("  -> MyriasGroup Resource Array Integration passed.")
