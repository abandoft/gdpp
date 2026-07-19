extends Node
class_name CrossRefB

var peer: CrossRefA
var peers_by_name: Dictionary[String, CrossRefA] = {}


func connect_peer(value: CrossRefA) -> CrossRefA:
    peer = value
    return peer


func typed_peers(values: Dictionary[String, CrossRefA]) -> Dictionary[String, CrossRefA]:
    peers_by_name = values
    return peers_by_name
