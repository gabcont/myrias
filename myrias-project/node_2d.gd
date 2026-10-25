extends Node2D

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	$FPSLabel.text = "FPS: " + str(Engine.get_frames_per_second())
	$CountLabel.text = "COUNT: " + str(MyriasServer.get_active_count())
