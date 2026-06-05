extends Node

@onready var example := MiniaudioClass.new()

func _ready() -> void:
	example.start()
func _process(delta: float) -> void:
	if Input.is_action_pressed("ui_accept"):
		var samp := example.get_samples()
		print(samp.size())
