extends Node

@onready var example := MiniaudioClass.new()

func _ready() -> void:
	example.start()
func _process(delta: float) -> void:
	if Input.is_action_pressed("ui_accept"):
		var samples = example.get_samples()
		var max_val = -1000.0
		for s in samples:
			if abs(s) > max_val:
				max_val = abs(s)
		print("Peak: ", max_val)
