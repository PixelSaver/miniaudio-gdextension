extends Node

@onready var example := MiniaudioClass.new()

func _ready() -> void:
	example.start()
func _process(delta: float) -> void:
	var samp := example.get_samples()
	print(samp.size())
