extends Node2D

func _ready() -> void:
	print("--- Running Kinematics Benchmark ---")
	print("SIMD Target: ", MyriasServer.get_simd_target_name())
	MyriasServer.set_soa_capacity(100000)
	var model : MyriasEntityModel = load("res://resources/bullet_model.tres")
	
	# Create 4 groups of 25,000 entities = 100,000 entities
	var gids = []
	for g in range(4):
		var gid = MyriasServer.create_group(model, 25000)
		gids.append(gid)
		for i in range(25000):
			MyriasServer.spawn(gid, Vector2(640, 360), Vector2(randf_range(-100, 100), randf_range(-100, 100)))

	# Wait 2 frames for spawn commands to execute
	await get_tree().process_frame
	await get_tree().process_frame

	var count = MyriasServer.get_active_count()
	print("Active entities: ", count)

	# Warmup 10 frames
	for f in range(10):
		await get_tree().process_frame

	# Measure over 100 frames
	var start_usec = Time.get_ticks_usec()
	for f in range(100):
		await get_tree().process_frame
	var end_usec = Time.get_ticks_usec()

	var total_ms = (end_usec - start_usec) / 1000.0
	var avg_frame_ms = total_ms / 100.0
	print("100 frames total time: %.2f ms" % total_ms)
	print("Average frame time (100k entities): %.3f ms" % avg_frame_ms)
	print("Average FPS: %.1f" % (1000.0 / avg_frame_ms if avg_frame_ms > 0 else 0))

	# Cleanup
	for gid in gids:
		MyriasServer.remove_group(gid)
	await get_tree().process_frame

	MyriasServer.set_soa_capacity(50000)

	get_tree().quit(0)
