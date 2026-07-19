extends Node
class_name CrossRefA

var peer: CrossRefB
var peers: Array[CrossRefB] = []


func connect_peer(value: CrossRefB) -> CrossRefB:
    peer = value
    return peer


func typed_peers(values: Array[CrossRefB]) -> Array[CrossRefB]:
    peers = values
    return peers
