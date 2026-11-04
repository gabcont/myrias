extends Node2D

@export var spawner : PackedScene = preload("res://scenes/spawner.tscn")
@export var max_spawners := 30
@export var time_between_spawners := 0.5
@export var time_between_removals := 1.0
var count := 0 
var creation_time_passed := 0.0 
var removal_time_passed := 0.0 
var state : States = States.CREATING

enum States {
	CREATING,
	REMOVING
}

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	pass # Replace with function body.


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	creation_time_passed += delta
	removal_time_passed += delta
	if creation_time_passed > time_between_spawners:
		if count < max_spawners:
			$Spawners.add_child(spawner.instantiate())
			creation_time_passed = 0.0
			count += 1
			print("spawned")
		else:
			state = States.REMOVING
	
	if removal_time_passed > time_between_removals:
		var idx := randi_range(0, get_child_count() - 1)
		if count > 0:
			removal_time_passed = 0.0
			$Spawners.remove_child($Spawners.get_child(idx))
			count -= 1
			print("Removed")
		else:
			state = States.CREATING
	# Called every frame. 'delta' is the elapsed time since the previous frame.
	$FPSLabel.text = "FPS: " + str(Engine.get_frames_per_second())
	$CountLabel.text = "COUNT: " + str(MyriasServer.get_active_count())

		
