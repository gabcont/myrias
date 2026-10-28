extends MyriasGroup

@export var speed := 50
@export var incremental := 0.01
@export var time_between_spawn := 0.01
var time_since_last_spawn : float = 0.0

var angle : float = 0

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	time_since_last_spawn += delta
	if time_since_last_spawn > time_between_spawn:
		spawn_one()
		time_since_last_spawn -= time_between_spawn
	

func spawn_one() : 
	angle += incremental
	MyriasServer.spawn(
		get_group_id(),
		$Marker2D.position,
		Vector2(cos(angle), sin(angle)) * speed,
	)
