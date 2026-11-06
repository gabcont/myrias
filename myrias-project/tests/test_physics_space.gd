extends Node2D

func _ready() -> void:
	print("--- Running Myrias Physics Space Tests ---")
	MyriasServer.configure_static_sdf(Rect2(0, 0, 1024, 1024), 4.0)
	await test_child_bridge_auto_detection()
	await test_static_sdf_accuracy_normals_and_layers()
	await test_runtime_mode_transitions()
	await test_collider_container_bridge()
	await test_debug_visualization_toggles()
	await test_physics_metadata_and_impulses()
	await test_entity_static_sdf_collision_and_conveyor()
	await test_entity_dynamic_rigidbody_two_way_impulse()
	await test_entity_area2d_space_modifiers_and_non_solid_sdf()
	await test_all_remaining_shape2ds()
	print("--- All Myrias Physics Space Tests PASSED! ---")
	get_tree().quit(0)

func step_frame() -> void:
	await get_tree().physics_frame
	await get_tree().process_frame

func make_circle_shape(radius: float) -> CollisionShape2D:
	var cs := CollisionShape2D.new()
	var shape := CircleShape2D.new()
	shape.radius = radius
	cs.shape = shape
	return cs

func make_rect_shape(size: Vector2) -> CollisionShape2D:
	var cs := CollisionShape2D.new()
	var shape := RectangleShape2D.new()
	shape.size = size
	cs.shape = shape
	return cs

func test_child_bridge_auto_detection() -> void:
	print("[Test 1] Child Bridge MyriasCollider Auto-Detection...")

	# 1. StaticBody2D -> defaults to STATIC
	var static_body := StaticBody2D.new()
	static_body.position = Vector2(200, 200)
	static_body.add_child(make_circle_shape(32.0))
	var col_static := MyriasCollider.new()
	static_body.add_child(col_static)
	add_child(static_body)

	# 2. CharacterBody2D -> defaults to DYNAMIC
	var char_body := CharacterBody2D.new()
	char_body.position = Vector2(400, 200)
	char_body.add_child(make_rect_shape(Vector2(40, 60)))
	var col_char := MyriasCollider.new()
	char_body.add_child(col_char)
	add_child(char_body)

	# 3. RigidBody2D -> defaults to DYNAMIC
	var rigid_body := RigidBody2D.new()
	rigid_body.position = Vector2(600, 200)
	rigid_body.gravity_scale = 0.0
	rigid_body.add_child(make_circle_shape(24.0))
	var col_rigid := MyriasCollider.new()
	rigid_body.add_child(col_rigid)
	add_child(rigid_body)

	# 4. Area2D -> defaults to DYNAMIC
	var area := Area2D.new()
	area.position = Vector2(800, 200)
	area.add_child(make_rect_shape(Vector2(50, 50)))
	var col_area := MyriasCollider.new()
	area.add_child(col_area)
	add_child(area)

	await step_frame()

	assert(col_static.get_collider_id() > 0, "StaticBody2D collider ID must be valid")
	assert(col_static.is_resolved_static() == true, "StaticBody2D must auto-resolve to STATIC")
	assert(col_char.get_collider_id() > 0, "CharacterBody2D collider ID must be valid")
	assert(col_char.is_resolved_static() == false, "CharacterBody2D must auto-resolve to DYNAMIC")
	assert(col_rigid.get_collider_id() > 0, "RigidBody2D collider ID must be valid")
	assert(col_rigid.is_resolved_static() == false, "RigidBody2D must auto-resolve to DYNAMIC")
	assert(col_area.get_collider_id() > 0, "Area2D collider ID must be valid")
	assert(col_area.is_resolved_static() == false, "Area2D must auto-resolve to DYNAMIC")

	assert(MyriasServer.get_static_collider_count() == 1, "Expected 1 static collider")
	assert(MyriasServer.get_dynamic_collider_count() == 3, "Expected 3 dynamic colliders")

	static_body.queue_free()
	char_body.queue_free()
	rigid_body.queue_free()
	area.queue_free()
	await step_frame()

	assert(MyriasServer.get_static_collider_count() == 0, "All static colliders cleaned up")
	assert(MyriasServer.get_dynamic_collider_count() == 0, "All dynamic colliders cleaned up")
	print("  -> Child Bridge Auto-Detection passed.")

func test_static_sdf_accuracy_normals_and_layers() -> void:
	print("[Test 2] Static Analytical SDF Accuracy, Normals & Layer Masks...")

	# Part A: Circle StaticBody2D at (300, 300), radius = 60, layer = 0b0010 (2)
	var circle_body := StaticBody2D.new()
	circle_body.position = Vector2(300, 300)
	circle_body.collision_layer = 2
	circle_body.add_child(make_circle_shape(60.0))
	var col_circle := MyriasCollider.new()
	circle_body.add_child(col_circle)
	add_child(circle_body)

	# Part B: Rotated Rectangle (OBB) StaticBody2D at (700, 500), size = (120, 60) -> half_extents = (60, 30), rotated 45 deg, layer = 0b0100 (4)
	var obb_body := StaticBody2D.new()
	obb_body.position = Vector2(700, 500)
	obb_body.rotation = PI / 4.0
	obb_body.collision_layer = 4
	obb_body.add_child(make_rect_shape(Vector2(120.0, 60.0)))
	var col_obb := MyriasCollider.new()
	obb_body.add_child(col_obb)
	add_child(obb_body)

	await step_frame()

	# Verify Circle SDF distances:
	# Center (300, 300): d should be approx -60
	var d_center := MyriasServer.sample_static_sdf_distance(Vector2(300, 300))
	assert(d_center < -55.0 and d_center > -65.0, "Circle center SDF expected ~ -60, got %.2f" % d_center)

	# Boundary (360, 300): d should be approx 0
	var d_boundary := MyriasServer.sample_static_sdf_distance(Vector2(360, 300))
	assert(abs(d_boundary) < 2.5, "Circle boundary SDF expected ~ 0, got %.2f" % d_boundary)

	# Exterior (400, 300): 100 px from center -> d should be approx +40
	var d_ext := MyriasServer.sample_static_sdf_distance(Vector2(400, 300))
	assert(abs(d_ext - 40.0) < 2.5, "Circle exterior SDF expected ~ +40, got %.2f" % d_ext)

	# Verify Circle outward normals:
	var n_right := MyriasServer.sample_static_sdf_normal(Vector2(360, 300))
	assert(n_right.dot(Vector2(1, 0)) > 0.98, "Normal at right of circle must point +X, got %s" % str(n_right))
	var n_top := MyriasServer.sample_static_sdf_normal(Vector2(300, 240))
	assert(n_top.dot(Vector2(0, -1)) > 0.98, "Normal at top of circle must point -Y, got %s" % str(n_top))

	# Verify Rotated OBB SDF distances and normals:
	# Center (700, 500): inside OBB, half_extents = (60, 30) -> distance to nearest face is -30
	var d_obb_center := MyriasServer.sample_static_sdf_distance(Vector2(700, 500))
	assert(abs(d_obb_center - (-30.0)) < 3.0, "OBB center SDF expected ~ -30, got %.2f" % d_obb_center)

	# Local +X axis is rotated by +45 deg: dir_x = (cos(45), sin(45))
	var dir_x := Vector2(cos(PI / 4.0), sin(PI / 4.0))
	# Point on +X face is center + dir_x * 60.0 -> d should be ~ 0, normal should be dir_x
	var pt_obb_face := Vector2(700, 500) + dir_x * 60.0
	var d_obb_face := MyriasServer.sample_static_sdf_distance(pt_obb_face)
	assert(abs(d_obb_face) < 3.0, "OBB rotated face SDF expected ~ 0, got %.2f" % d_obb_face)

	var n_obb_face := MyriasServer.sample_static_sdf_normal(pt_obb_face + dir_x * 12.0)
	assert(n_obb_face.dot(dir_x) > 0.96, "OBB rotated face normal must align with rotated axis, got %s vs %s" % [str(n_obb_face), str(dir_x)])

	# Verify collision_layer tagging & mask filtering:
	assert(MyriasServer.sample_static_sdf_layer(Vector2(300, 300)) == 2, "Near circle, SDF layer must be 2")
	assert(MyriasServer.sample_static_sdf_layer(Vector2(700, 500)) == 4, "Near OBB, SDF layer must be 4")

	# Sampling circle with mask=2 succeeds, with mask=4 returns 1e6 (filtered out)
	assert(MyriasServer.sample_static_sdf_distance(Vector2(300, 300), 2) < 0.0, "Mask 2 should hit circle on layer 2")
	assert(MyriasServer.sample_static_sdf_distance(Vector2(300, 300), 4) >= 1e5, "Mask 4 should ignore circle on layer 2")

	circle_body.queue_free()
	obb_body.queue_free()
	await step_frame()
	print("  -> Static Analytical SDF Accuracy, Normals & Layer Masks passed.")

func test_runtime_mode_transitions() -> void:
	print("[Test 3] Runtime Mode Transition (STATIC <-> DYNAMIC)...")

	var body := StaticBody2D.new()
	body.position = Vector2(250, 250)
	body.add_child(make_circle_shape(50.0))
	var col := MyriasCollider.new()
	body.add_child(col)
	add_child(body)
	await step_frame()

	var cid := col.get_collider_id()
	assert(col.is_resolved_static() == true, "Initially resolved as STATIC")
	assert(MyriasServer.get_static_collider_count() == 1, "1 static collider")
	assert(MyriasServer.get_dynamic_collider_count() == 0, "0 dynamic colliders")
	assert(MyriasServer.sample_static_sdf_distance(Vector2(250, 250)) < -40.0, "Baked in SDF at (250, 250)")

	# Transition to MODE_DYNAMIC at runtime!
	col.mode = MyriasCollider.MODE_DYNAMIC
	await step_frame()

	assert(col.is_resolved_static() == false, "Now resolved as DYNAMIC")
	assert(MyriasServer.get_static_collider_count() == 0, "0 static colliders after transition")
	assert(MyriasServer.get_dynamic_collider_count() == 1, "1 dynamic collider after transition")
	# SDF should have re-baked without this body (empty SDF returns 1e6)
	assert(MyriasServer.sample_static_sdf_distance(Vector2(250, 250)) >= 1e5, "SDF must clear removed static body")

	# Move dynamic body to (600, 600) and verify dynamic AABB updates on physics_tick
	body.position = Vector2(600, 600)
	await step_frame()

	var aabb := MyriasServer.get_collider_aabb(cid)
	assert(is_equal_approx(aabb.position.x, 550.0) and is_equal_approx(aabb.position.y, 550.0), "Dynamic AABB position must update to (550, 550), got %s" % str(aabb))
	assert(is_equal_approx(aabb.size.x, 100.0) and is_equal_approx(aabb.size.y, 100.0), "Dynamic AABB size must be (100, 100)")

	# Transition back to MODE_STATIC at its new position (600, 600)!
	col.mode = MyriasCollider.MODE_STATIC
	await step_frame()

	assert(col.is_resolved_static() == true, "Back to STATIC")
	assert(MyriasServer.get_static_collider_count() == 1, "1 static collider")
	assert(MyriasServer.get_dynamic_collider_count() == 0, "0 dynamic colliders")
	assert(MyriasServer.sample_static_sdf_distance(Vector2(600, 600)) < -40.0, "SDF re-baked at new position (600, 600)")
	assert(MyriasServer.sample_static_sdf_distance(Vector2(250, 250)) > 250.0, "Old position (250, 250) is outside")

	body.queue_free()
	await step_frame()
	print("  -> Runtime Mode Transition passed.")

func test_collider_container_bridge() -> void:
	print("[Test 4] Container Bridge MyriasColliderContainer...")

	var container := MyriasColliderContainer.new()
	container.mode = MyriasCollider.MODE_AUTO

	var b1 := StaticBody2D.new()
	b1.position = Vector2(150, 150)
	b1.add_child(make_circle_shape(30.0))
	container.add_child(b1)

	var b2 := CharacterBody2D.new()
	b2.position = Vector2(350, 150)
	b2.add_child(make_rect_shape(Vector2(40, 40)))
	container.add_child(b2)

	add_child(container)
	await step_frame()

	assert(container.get_registered_collider_count() == 2, "Container should register 2 initial children")
	assert(MyriasServer.get_static_collider_count() == 1, "1 static child (b1)")
	assert(MyriasServer.get_dynamic_collider_count() == 1, "1 dynamic child (b2)")

	# Dynamically add a 3rd child at runtime
	var b3 := StaticBody2D.new()
	b3.position = Vector2(550, 150)
	b3.add_child(make_circle_shape(25.0))
	container.add_child(b3)
	await step_frame()

	assert(container.get_registered_collider_count() == 3, "Container should auto-register newly added child b3")
	assert(MyriasServer.get_static_collider_count() == 2, "2 static children (b1, b3)")

	# Override container mode to MODE_DYNAMIC -> all 3 become dynamic!
	container.mode = MyriasCollider.MODE_DYNAMIC
	await step_frame()

	assert(MyriasServer.get_static_collider_count() == 0, "All container children switched to dynamic")
	assert(MyriasServer.get_dynamic_collider_count() == 3, "3 dynamic children")

	# Remove child b3 at runtime
	container.remove_child(b3)
	b3.free()
	await step_frame()

	assert(container.get_registered_collider_count() == 2, "Container should auto-unregister removed child b3")
	assert(MyriasServer.get_dynamic_collider_count() == 2, "2 dynamic children remaining")

	container.queue_free()
	await step_frame()

	assert(MyriasServer.get_static_collider_count() == 0, "0 static after container free")
	assert(MyriasServer.get_dynamic_collider_count() == 0, "0 dynamic after container free")
	print("  -> Container Bridge MyriasColliderContainer passed.")

func test_debug_visualization_toggles() -> void:
	print("[Test 5] Debug Visualization Toggles...")

	assert(ProjectSettings.has_setting("myrias/debug/draw_static_sdf"), "ProjectSetting myrias/debug/draw_static_sdf must exist")
	assert(ProjectSettings.has_setting("myrias/debug/draw_dynamic_colliders"), "ProjectSetting myrias/debug/draw_dynamic_colliders must exist")

	var s_body := StaticBody2D.new()
	s_body.position = Vector2(300, 300)
	s_body.add_child(make_circle_shape(45.0))
	s_body.add_child(MyriasCollider.new())
	add_child(s_body)

	var d_body := CharacterBody2D.new()
	d_body.position = Vector2(500, 300)
	d_body.rotation = 0.4
	d_body.add_child(make_rect_shape(Vector2(60, 30)))
	d_body.add_child(MyriasCollider.new())
	add_child(d_body)

	MyriasServer.set_debug_draw_static_sdf(true)
	MyriasServer.set_debug_draw_dynamic_colliders(true)
	assert(MyriasServer.is_debug_draw_static_sdf_enabled() == true)
	assert(MyriasServer.is_debug_draw_dynamic_colliders_enabled() == true)

	await step_frame()
	await step_frame()

	MyriasServer.set_debug_draw_static_sdf(false)
	MyriasServer.set_debug_draw_dynamic_colliders(false)
	assert(MyriasServer.is_debug_draw_static_sdf_enabled() == false)
	assert(MyriasServer.is_debug_draw_dynamic_colliders_enabled() == false)

	await step_frame()

	s_body.queue_free()
	d_body.queue_free()
	await step_frame()
	print("  -> Debug Visualization Toggles passed.")

func test_physics_metadata_and_impulses() -> void:
	print("[Test 6] Physics Metadata Extraction, Material Combine & RigidBody2D Impulses...")

	# 1. StaticBody2D with absorbent & rough PhysicsMaterial + constant conveyor velocities
	var s_body := StaticBody2D.new()
	s_body.position = Vector2(200.0, 200.0)
	s_body.constant_linear_velocity = Vector2(50.0, 0.0)
	s_body.constant_angular_velocity = 2.0
	var s_mat := PhysicsMaterial.new()
	s_mat.bounce = 0.8
	s_mat.absorbent = true
	s_mat.friction = 0.4
	s_mat.rough = true
	s_body.physics_material_override = s_mat
	s_body.add_child(make_circle_shape(32.0))
	var col_static := MyriasCollider.new()
	s_body.add_child(col_static)
	add_child(s_body)

	# 2. RigidBody2D with mass = 4.0 and PhysicsMaterial (bounce = 0.5, friction = 0.5)
	var r_body := RigidBody2D.new()
	r_body.position = Vector2(500.0, 200.0)
	r_body.gravity_scale = 0.0
	r_body.mass = 4.0
	var r_mat := PhysicsMaterial.new()
	r_mat.bounce = 0.5
	r_mat.friction = 0.5
	r_body.physics_material_override = r_mat
	r_body.add_child(make_circle_shape(20.0))
	var col_rigid := MyriasCollider.new()
	r_body.add_child(col_rigid)
	add_child(r_body)

	# 3. CharacterBody2D with velocity = Vector2(150.0, -50.0)
	var c_body := CharacterBody2D.new()
	c_body.position = Vector2(700.0, 200.0)
	c_body.velocity = Vector2(150.0, -50.0)
	c_body.add_child(make_rect_shape(Vector2(30.0, 30.0)))
	var col_char := MyriasCollider.new()
	c_body.add_child(col_char)
	add_child(c_body)

	# 4. Area2D with space overrides
	var area := Area2D.new()
	area.position = Vector2(850.0, 200.0)
	area.gravity_space_override = Area2D.SPACE_OVERRIDE_REPLACE
	area.gravity = 500.0
	area.gravity_point = true
	area.gravity_point_unit_distance = 64.0
	area.linear_damp_space_override = Area2D.SPACE_OVERRIDE_COMBINE
	area.linear_damp = 2.5
	area.angular_damp = 3.0
	area.priority = 7
	area.add_child(make_circle_shape(40.0))
	var col_area := MyriasCollider.new()
	area.add_child(col_area)
	add_child(area)

	await step_frame()

	# Verify StaticBody2D metadata
	var s_cid := col_static.get_collider_id()
	assert(is_equal_approx(MyriasServer.get_collider_bounce(s_cid), -0.8), "Absorbent bounce must be signed -0.8, got %.3f" % MyriasServer.get_collider_bounce(s_cid))
	assert(is_equal_approx(MyriasServer.get_collider_friction(s_cid), -0.4), "Rough friction must be signed -0.4, got %.3f" % MyriasServer.get_collider_friction(s_cid))
	assert(is_equal_approx(MyriasServer.get_collider_inv_mass(s_cid), 0.0), "StaticBody2D inv_mass must be 0.0")
	# At (200, 210), r = (0, 10) -> v = (50, 0) + (-2.0 * 10, 2.0 * 0) = (30, 0)
	var pt_vel := MyriasServer.get_collider_point_velocity(s_cid, Vector2(200.0, 210.0))
	assert(pt_vel.distance_to(Vector2(30.0, 0.0)) < 0.01, "Expected point velocity (30, 0), got %s" % str(pt_vel))

	# Verify RigidBody2D metadata & impulses
	var r_cid := col_rigid.get_collider_id()
	assert(is_equal_approx(MyriasServer.get_collider_mass(r_cid), 4.0), "RigidBody2D mass must be 4.0")
	assert(is_equal_approx(MyriasServer.get_collider_inv_mass(r_cid), 0.25), "RigidBody2D inv_mass must be 0.25")
	assert(MyriasServer.get_collider_inertia(r_cid) > 0.0, "RigidBody2D inertia must be > 0")
	assert(MyriasServer.get_collider_inv_inertia(r_cid) > 0.0, "RigidBody2D inv_inertia must be > 0")
	assert(is_equal_approx(MyriasServer.get_collider_bounce(r_cid), 0.5), "RigidBody2D bounce must be 0.5")
	assert(is_equal_approx(MyriasServer.get_collider_friction(r_cid), 0.5), "RigidBody2D friction must be 0.5")

	# Apply off-center impulse at (500, 210) -> r = (0, 10), impulse = (40, 0)
	# delta_v = (40, 0) * 0.25 = (10, 0)
	# torque = r.cross(impulse) = 0*0 - 10*40 = -400 -> angular_velocity < 0
	MyriasServer.apply_collider_impulse(r_cid, Vector2(40.0, 0.0), Vector2(500.0, 210.0))
	var r_lin_vel := MyriasServer.get_collider_linear_velocity(r_cid)
	var r_ang_vel := MyriasServer.get_collider_angular_velocity(r_cid)
	assert(r_lin_vel.distance_to(Vector2(10.0, 0.0)) < 0.05, "Expected linear_velocity (10, 0) after impulse, got %s" % str(r_lin_vel))
	assert(r_ang_vel < -0.1, "Expected negative angular_velocity after off-center impulse, got %.3f" % r_ang_vel)

	# Freeze RigidBody2D and verify inv_mass & inv_inertia become 0.0 on next tick
	r_body.freeze = true
	await step_frame()
	assert(is_equal_approx(MyriasServer.get_collider_inv_mass(r_cid), 0.0), "Frozen RigidBody2D inv_mass must be 0.0")
	assert(is_equal_approx(MyriasServer.get_collider_inv_inertia(r_cid), 0.0), "Frozen RigidBody2D inv_inertia must be 0.0")

	# Verify CharacterBody2D metadata
	var c_cid := col_char.get_collider_id()
	assert(is_equal_approx(MyriasServer.get_collider_inv_mass(c_cid), 0.0), "CharacterBody2D inv_mass must be 0.0")
	var c_vel := MyriasServer.get_collider_linear_velocity(c_cid)
	assert(c_vel.distance_to(Vector2(150.0, -50.0)) < 0.01, "CharacterBody2D linear_velocity must match velocity property, got %s" % str(c_vel))

	# Verify Area2D space override info
	var a_cid := col_area.get_collider_id()
	var a_info := MyriasServer.get_area_space_override_info(a_cid)
	assert(a_info.get("is_area", false) == true, "Area2D is_area must be true")
	assert(a_info.get("priority", 0) == 7, "Area2D priority must be 7")
	assert(a_info.get("gravity_override_mode", 0) == Area2D.SPACE_OVERRIDE_REPLACE, "Area2D gravity_override_mode must be REPLACE")
	assert(is_equal_approx(float(a_info.get("gravity", 0.0)), 500.0), "Area2D gravity must be 500.0")
	assert(a_info.get("gravity_is_point", false) == true, "Area2D gravity_is_point must be true")
	assert(is_equal_approx(float(a_info.get("gravity_point_unit_distance", 0.0)), 64.0), "Area2D gravity_point_unit_distance must be 64.0")
	assert(is_equal_approx(float(a_info.get("linear_damp", 0.0)), 2.5), "Area2D linear_damp must be 2.5")
	assert(is_equal_approx(float(a_info.get("angular_damp", 0.0)), 3.0), "Area2D angular_damp must be 3.0")

	# Verify Godot 2D material combination rules
	# Bounce: CLAMP(a + b, 0.0, 1.0)
	assert(is_equal_approx(MyriasServer.combine_bounce(0.6, 0.7), 1.0), "combine_bounce(0.6, 0.7) -> 1.0")
	assert(is_equal_approx(MyriasServer.combine_bounce(0.6, -0.4), 0.2), "combine_bounce(0.6, -0.4) -> 0.2 (absorbent)")
	assert(is_equal_approx(MyriasServer.combine_bounce(0.3, -0.8), 0.0), "combine_bounce(0.3, -0.8) -> 0.0 (clamped)")
	# Friction: if either < 0 (rough) -> abs(min(a, b)), else a * b
	assert(is_equal_approx(MyriasServer.combine_friction(0.5, 0.5), 0.25), "combine_friction(0.5, 0.5) -> 0.25")
	assert(is_equal_approx(MyriasServer.combine_friction(0.5, -0.8), 0.8), "combine_friction(0.5, -0.8) -> 0.8 (rough)")
	assert(is_equal_approx(MyriasServer.combine_friction(-0.9, -0.4), 0.9), "combine_friction(-0.9, -0.4) -> 0.9 (both rough)")

	s_body.queue_free()
	r_body.queue_free()
	c_body.queue_free()
	area.queue_free()
	await step_frame()
	print("  -> Physics Metadata Extraction, Material Combine & RigidBody2D Impulses passed.")

func test_entity_static_sdf_collision_and_conveyor() -> void:
	print("[Test 7] Entity vs Static SDF Bounce, Conveyor Friction & Per-Entity TRAIT_PHYSICS Toggle...")
	MyriasServer.configure_spatial_grid(Rect2(0, 0, 1024, 1024), 32.0)

	# Static vertical wall at (300, 300) with size (40, 200) -> left face at x = 280
	# Conveyor belt moving downward (+Y) at 200 px/s
	var wall := StaticBody2D.new()
	wall.position = Vector2(300.0, 300.0)
	wall.constant_linear_velocity = Vector2(0.0, 200.0)
	var wall_mat := PhysicsMaterial.new()
	wall_mat.bounce = 0.5
	wall_mat.friction = 0.8
	wall.physics_material_override = wall_mat
	wall.add_child(make_rect_shape(Vector2(40.0, 200.0)))
	wall.add_child(MyriasCollider.new())
	add_child(wall)

	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	model.radius = 8.0
	var gid := MyriasServer.create_group(model, 16)

	var phys_trait := MyriasTraitPhysics.new()
	phys_trait.mass = 1.0
	phys_trait.bounce = 0.5
	phys_trait.friction = 1.0
	MyriasServer.configure_trait(gid, phys_trait)

	# Entity 0: at x = 275 (overlapping left face at x = 280 since r = 8), moving +X into wall
	MyriasServer.spawn(gid, Vector2(275.0, 300.0), Vector2(100.0, 0.0))
	# Entity 1: at x = 275, moving +X, but with TRAIT_PHYSICS disabled!
	MyriasServer.spawn(gid, Vector2(275.0, 340.0), Vector2(100.0, 0.0))
	MyriasServer.set_entity_trait(gid, 1, MyriasServer.TRAIT_PHYSICS, false)

	await step_frame()

	# Entity 0 must have bounced off the wall (vel.x < -50) and picked up +Y velocity from the conveyor!
	var pos0 := MyriasServer.get_entity_position(gid, 0)
	var vel0 := MyriasServer.get_entity_velocity(gid, 0)
	assert(pos0.x <= 272.5, "Entity 0 must be pushed out left of wall face (x <= 272.5), got %.2f" % pos0.x)
	assert(vel0.x < -50.0, "Entity 0 must bounce back with negative vel.x, got %.2f" % vel0.x)
	assert(vel0.y > 50.0, "Entity 0 must gain +Y velocity from conveyor friction, got %.2f" % vel0.y)

	# Entity 1 (physics disabled) must ignore the wall and keep vel = (100, 0)
	var vel1 := MyriasServer.get_entity_velocity(gid, 1)
	assert(is_equal_approx(vel1.x, 100.0) and is_equal_approx(vel1.y, 0.0), "Entity 1 without TRAIT_PHYSICS must not collide, got %s" % str(vel1))

	MyriasServer.remove_group(gid)
	wall.queue_free()
	await step_frame()
	print("  -> Entity vs Static SDF Bounce, Conveyor Friction & Per-Entity TRAIT_PHYSICS Toggle passed.")

func test_entity_dynamic_rigidbody_two_way_impulse() -> void:
	print("[Test 8] Entity vs Dynamic RigidBody2D Two-Way Impulse Transfer...")
	MyriasServer.configure_spatial_grid(Rect2(0, 0, 1024, 1024), 32.0)

	# Dynamic RigidBody2D circle of radius 24 at (500, 300)
	var r_body := RigidBody2D.new()
	r_body.position = Vector2(500.0, 300.0)
	r_body.gravity_scale = 0.0
	r_body.mass = 2.0
	var r_mat := PhysicsMaterial.new()
	r_mat.bounce = 0.5
	r_mat.friction = 0.5
	r_body.physics_material_override = r_mat
	r_body.add_child(make_circle_shape(24.0))
	var col_rigid := MyriasCollider.new()
	r_body.add_child(col_rigid)
	add_child(r_body)

	# Wait 1 frame so RigidBody2D is registered in PhysicsServer2D and MyriasPhysicsSpace
	await step_frame()
	var r_cid := col_rigid.get_collider_id()

	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	model.radius = 10.0
	var gid := MyriasServer.create_group(model, 16)

	var phys_trait := MyriasTraitPhysics.new()
	phys_trait.mass = 2.0
	phys_trait.bounce = 0.5
	phys_trait.friction = 0.5
	MyriasServer.configure_trait(gid, phys_trait)

	# Spawn entity at (472, 308): distance to (500, 300) is sqrt(28^2 + 8^2) = 29.12 < (24 + 10 = 34)
	# Moving fast to the right (+X) at (300, 0) so it impacts the RigidBody2D off-center
	MyriasServer.spawn(gid, Vector2(472.0, 308.0), Vector2(300.0, 0.0))
	await step_frame()

	var ent_vel := MyriasServer.get_entity_velocity(gid, 0)
	var body_vel := MyriasServer.get_collider_linear_velocity(r_cid)
	var body_ang := MyriasServer.get_collider_angular_velocity(r_cid)

	assert(ent_vel.x < 100.0, "Entity velocity X must be reduced/reversed by collision with RigidBody2D, got %.2f" % ent_vel.x)
	assert(body_vel.x > 50.0, "RigidBody2D must receive +X impulse from entity impact, got %s" % str(body_vel))
	assert(abs(body_ang) > 0.05, "Off-center entity impact must impart angular velocity to RigidBody2D, got %.3f" % body_ang)

	MyriasServer.remove_group(gid)
	r_body.queue_free()
	await step_frame()
	print("  -> Entity vs Dynamic RigidBody2D Two-Way Impulse Transfer passed.")

func test_entity_area2d_space_modifiers_and_non_solid_sdf() -> void:
	print("[Test 9] Area2D Space Modifiers (Point Gravity & Linear Damp) & Non-Solid SDF Exclusion...")
	MyriasServer.configure_spatial_grid(Rect2(0, 0, 1024, 1024), 32.0)

	# Create an Area2D configured as MODE_STATIC at (400, 300) with radius 80
	var area := Area2D.new()
	area.position = Vector2(400.0, 300.0)
	area.gravity_space_override = Area2D.SPACE_OVERRIDE_REPLACE
	area.gravity_point = true
	area.gravity_point_center = Vector2.ZERO
	area.gravity = 1200.0
	area.linear_damp_space_override = Area2D.SPACE_OVERRIDE_REPLACE
	area.linear_damp = 5.0
	area.add_child(make_circle_shape(80.0))
	var col_area := MyriasCollider.new()
	col_area.mode = MyriasCollider.MODE_STATIC
	area.add_child(col_area)
	add_child(area)

	await step_frame()

	# 1. Verify that static Area2D does NOT bake into the solid Static SDF!
	var sdf_at_area := MyriasServer.sample_static_sdf_distance(Vector2(400.0, 300.0))
	assert(sdf_at_area >= 1e5, "Area2D must NEVER bake as a solid obstacle in the Static SDF, got %.2f" % sdf_at_area)

	# 2. Spawn an entity inside the Area2D at (350, 300) (50 px left of center (400, 300))
	# with initial velocity purely along +Y: (0, 200)
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	model.radius = 8.0
	var gid := MyriasServer.create_group(model, 16)

	var phys_trait := MyriasTraitPhysics.new()
	phys_trait.gravity_scale = 1.0
	phys_trait.linear_damp = 0.0
	MyriasServer.configure_trait(gid, phys_trait)

	MyriasServer.spawn(gid, Vector2(350.0, 300.0), Vector2(0.0, 200.0))
	await step_frame()

	var vel := MyriasServer.get_entity_velocity(gid, 0)
	var pos := MyriasServer.get_entity_position(gid, 0)
	# Point gravity pulls toward (400, 300), so vel.x must become positive (> 10.0)
	assert(vel.x > 10.0, "Area2D point gravity must accelerate entity toward center (+X), got %.2f" % vel.x)
	# Linear damp (5.0) must reduce initial vel.y below 200.0
	assert(vel.y < 195.0, "Area2D linear_damp must damp entity Y velocity below 200.0, got %.2f" % vel.y)
	# Entity must remain inside the Area2D (not pushed out to radius 80)
	assert(pos.distance_to(Vector2(400.0, 300.0)) < 60.0, "Area2D must not push entity out like a solid collider")

	MyriasServer.remove_group(gid)
	area.queue_free()
	await step_frame()
	print("  -> Area2D Space Modifiers & Non-Solid SDF Exclusion passed.")

func test_all_remaining_shape2ds() -> void:
	print("[Test 10] All Remaining 2D Shape Types (Capsule, Segment, SeparationRay, WorldBoundary, ConvexPolygon, ConcavePolygon, CollisionPolygon2D)...")
	MyriasServer.configure_spatial_grid(Rect2(0, 0, 1024, 1024), 32.0)

	# 1. CapsuleShape2D (StaticBody2D at (200, 300), radius=20, height=100 -> hl=30, spine Y in [270, 330], total Y in [250, 350])
	var cap_body := StaticBody2D.new()
	cap_body.position = Vector2(200.0, 300.0)
	var cap_cs := CollisionShape2D.new()
	var cap_shape := CapsuleShape2D.new()
	cap_shape.radius = 20.0
	cap_shape.height = 100.0
	cap_cs.shape = cap_shape
	cap_body.add_child(cap_cs)
	cap_body.add_child(MyriasCollider.new())
	add_child(cap_body)

	# 2. SegmentShape2D (CharacterBody2D at (400, 300), segment from (0, -40) to (0, 40))
	var seg_body := CharacterBody2D.new()
	seg_body.position = Vector2(400.0, 300.0)
	var seg_cs := CollisionShape2D.new()
	var seg_shape := SegmentShape2D.new()
	seg_shape.a = Vector2(0.0, -40.0)
	seg_shape.b = Vector2(0.0, 40.0)
	seg_cs.shape = seg_shape
	seg_body.add_child(seg_cs)
	seg_body.add_child(MyriasCollider.new())
	add_child(seg_body)

	# 3. SeparationRayShape2D (CharacterBody2D at (550, 300), rotated -90 deg so local +Y points +X, length=40 -> ray from (550, 300) to (590, 300) pushing +X)
	var ray_body := CharacterBody2D.new()
	ray_body.position = Vector2(550.0, 300.0)
	ray_body.rotation = -PI / 2.0
	var ray_cs := CollisionShape2D.new()
	var ray_shape := SeparationRayShape2D.new()
	ray_shape.length = 40.0
	ray_cs.shape = ray_shape
	ray_body.add_child(ray_cs)
	ray_body.add_child(MyriasCollider.new())
	add_child(ray_body)

	# 4. ConvexPolygonShape2D via CollisionPolygon2D (BUILD_SOLIDS) on StaticBody2D at (750, 300) (box [-30..30, -30..30])
	var poly_solid_body := StaticBody2D.new()
	poly_solid_body.position = Vector2(750.0, 300.0)
	var cp_solid := CollisionPolygon2D.new()
	cp_solid.build_mode = CollisionPolygon2D.BUILD_SOLIDS
	cp_solid.polygon = PackedVector2Array([
		Vector2(-30.0, -30.0),
		Vector2(30.0, -30.0),
		Vector2(30.0, 30.0),
		Vector2(-30.0, 30.0)
	])
	poly_solid_body.add_child(cp_solid)
	poly_solid_body.add_child(MyriasCollider.new())
	add_child(poly_solid_body)

	# 5. ConcavePolygonShape2D on CharacterBody2D at (880, 300) (vertical segment from (0, -40) to (0, 40))
	var concave_body := CharacterBody2D.new()
	concave_body.position = Vector2(880.0, 300.0)
	var concave_cs := CollisionShape2D.new()
	var concave_shape := ConcavePolygonShape2D.new()
	concave_shape.segments = PackedVector2Array([Vector2(0.0, -40.0), Vector2(0.0, 40.0)])
	concave_cs.shape = concave_shape
	concave_body.add_child(concave_cs)
	concave_body.add_child(MyriasCollider.new())
	add_child(concave_body)

	await step_frame()

	# Verify Static SDF for CapsuleShape2D at (200, 300):
	# Center (200, 300) is on spine -> SDF = -20
	var d_cap_center := MyriasServer.sample_static_sdf_distance(Vector2(200.0, 300.0))
	assert(abs(d_cap_center - (-20.0)) < 2.5, "Capsule center SDF expected ~ -20, got %.2f" % d_cap_center)
	# Top cap boundary at (200, 250) -> SDF = 0
	var d_cap_top := MyriasServer.sample_static_sdf_distance(Vector2(200.0, 250.0))
	assert(abs(d_cap_top) < 2.5, "Capsule top tip SDF expected ~ 0, got %.2f" % d_cap_top)

	# Verify Static SDF for ConvexPolygonShape2D (CollisionPolygon2D BUILD_SOLIDS) at (750, 300):
	# Center (750, 300) -> SDF = -30 (inside)
	var d_poly_in := MyriasServer.sample_static_sdf_distance(Vector2(750.0, 300.0))
	assert(abs(d_poly_in - (-30.0)) < 3.0, "Convex polygon center SDF expected ~ -30, got %.2f" % d_poly_in)
	# Left face at (720, 300) -> SDF = 0
	var d_poly_edge := MyriasServer.sample_static_sdf_distance(Vector2(720.0, 300.0))
	assert(abs(d_poly_edge) < 2.5, "Convex polygon edge SDF expected ~ 0, got %.2f" % d_poly_edge)

	# Test Entity collisions with Capsule, Segment, SeparationRay, ConvexPolygon, and ConcavePolygon
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	model.radius = 8.0
	var gid := MyriasServer.create_group(model, 16)
	var phys_trait := MyriasTraitPhysics.new()
	phys_trait.bounce = 0.8
	phys_trait.friction = 0.0
	MyriasServer.configure_trait(gid, phys_trait)

	# Entity 0 -> hits Capsule left side at x = 180 (spawn at 175 moving +X)
	MyriasServer.spawn(gid, Vector2(175.0, 300.0), Vector2(100.0, 0.0))
	# Entity 1 -> hits Segment at x = 400 (spawn at 395 moving +X)
	MyriasServer.spawn(gid, Vector2(395.0, 300.0), Vector2(100.0, 0.0))
	# Entity 2 -> overlaps SeparationRay at (570, 300) moving -X (-100, 0) -> pushed along +X ray!
	MyriasServer.spawn(gid, Vector2(570.0, 300.0), Vector2(-100.0, 0.0))
	# Entity 3 -> hits ConvexPolygon left face at x = 720 (spawn at 715 moving +X)
	MyriasServer.spawn(gid, Vector2(715.0, 300.0), Vector2(100.0, 0.0))
	# Entity 4 -> hits ConcavePolygon segment at x = 880 (spawn at 875 moving +X)
	MyriasServer.spawn(gid, Vector2(875.0, 300.0), Vector2(100.0, 0.0))

	# Enable debug draw for 1 frame to exercise all shape debug render paths
	MyriasServer.set_debug_draw_dynamic_colliders(true)
	await step_frame()
	MyriasServer.set_debug_draw_dynamic_colliders(false)

	assert(MyriasServer.get_entity_velocity(gid, 0).x < -50.0, "Entity 0 must bounce off CapsuleShape2D")
	assert(MyriasServer.get_entity_velocity(gid, 1).x < -50.0, "Entity 1 must bounce off SegmentShape2D")
	assert(MyriasServer.get_entity_velocity(gid, 2).x > 50.0, "Entity 2 must be separated along +X by SeparationRayShape2D")
	assert(MyriasServer.get_entity_velocity(gid, 3).x < -50.0, "Entity 3 must bounce off ConvexPolygonShape2D")
	assert(MyriasServer.get_entity_velocity(gid, 4).x < -50.0, "Entity 4 must bounce off ConcavePolygonShape2D")

	MyriasServer.remove_group(gid)
	cap_body.queue_free()
	seg_body.queue_free()
	ray_body.queue_free()
	poly_solid_body.queue_free()
	concave_body.queue_free()
	await step_frame()

	# 6. WorldBoundaryShape2D (Infinite horizontal floor at y = 600 with normal (0, -1))
	var wb_body := StaticBody2D.new()
	wb_body.position = Vector2(0.0, 600.0)
	var wb_cs := CollisionShape2D.new()
	var wb_shape := WorldBoundaryShape2D.new()
	wb_shape.normal = Vector2(0.0, -1.0)
	wb_shape.distance = 0.0
	wb_cs.shape = wb_shape
	wb_body.add_child(wb_cs)
	wb_body.add_child(MyriasCollider.new())
	add_child(wb_body)
	await step_frame()

	# Above floor at y = 550 -> SDF = +50; Below floor at y = 620 -> SDF = -20
	var d_wb_above := MyriasServer.sample_static_sdf_distance(Vector2(500.0, 550.0))
	var d_wb_below := MyriasServer.sample_static_sdf_distance(Vector2(500.0, 620.0))
	var n_wb := MyriasServer.sample_static_sdf_normal(Vector2(500.0, 590.0))
	assert(abs(d_wb_above - 50.0) < 2.5, "WorldBoundary SDF at y=550 expected +50, got %.2f" % d_wb_above)
	assert(abs(d_wb_below - (-20.0)) < 2.5, "WorldBoundary SDF at y=620 expected -20, got %.2f" % d_wb_below)
	assert(n_wb.dot(Vector2(0.0, -1.0)) > 0.98, "WorldBoundary normal must point (0, -1), got %s" % str(n_wb))

	wb_body.queue_free()
	await step_frame()
	print("  -> All Remaining 2D Shape Types passed.")
