extends MyriasGroup

@export var speed := 50
var angle : float = 0

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	for i in range(10):
		spawn_one()
	

func spawn_one() : 
	angle += 0.01
	MyriasServer.spawn(
		get_group_id(),
		$Marker2D.position,
		Vector2(cos(angle), sin(angle)) * speed,
	)
