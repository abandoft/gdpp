extends Node

signal resumed

var checkpoint: int = 0


func run(enabled: bool) -> void:
	checkpoint = 1
	await resumed
	checkpoint = 2
	await resumed
	checkpoint = 3
	await resumed
	checkpoint = 4
	if enabled:
		await resumed
		checkpoint = 5
	else:
		await resumed
		checkpoint = -5
	await resumed
	checkpoint = 6
	await resumed
	checkpoint = 7
	await resumed
	checkpoint = 8
	await resumed
	checkpoint = 9
	await resumed
	checkpoint = 10
	await resumed
	checkpoint = 11
