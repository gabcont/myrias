extends Node2D

func _ready() -> void:
	print("--- Running Myrias Physics Space Tests ---")
	MyriasServer.configure_static_sdf(Rect2(0, 0, 1024, 1024), 4.0)
	await test_child_bridge_auto_detection()
	await test_static_sdf_accuracy_normals_and_layers()
	await test_runtime_mode_transitions()
	await test_collider_container_bridge()
	await test_debug_visualization_toggles()
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
