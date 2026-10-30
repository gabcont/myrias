extends MyriasGroup

@export var speed := 50
@export var incremental := 0.01
@export var time_between_spawn := 0.01
@export var random : bool = false
var time_since_last_spawn : float = 0.0

var angle : float = 0

func _ready() -> void:
	if random:
		rand_init()

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	time_since_last_spawn += delta
	if time_since_last_spawn > time_between_spawn:
		spawn_one()
		spawn_one()
		spawn_one()
		time_since_last_spawn -= time_between_spawn
	

func spawn_one() : 
	angle += incremental
	MyriasServer.spawn(
		get_group_id(),
		$Marker2D.position,
		Vector2(cos(angle), sin(angle)) * speed,
	)

func rand_init() : 
	speed = randi_range(50, 100)
	incremental = randf_range(0.01, 0.1)
	time_between_spawn = randf_range(0.05, 0.1)
	var vp = Vector2(1280.0, 720.0)
	$Marker2D.position = Vector2(randf_range(0, vp.x), randf_range(0, vp.y))
	modulate = Color(randf(),randf(),randf(),1)
