"""Retrieve only the four pinned runtime JARs used for Fish WebSocket + MessagePack."""
from pathlib import Path
import hashlib, sys, urllib.request
root=Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
pins={
'org/java-websocket/Java-WebSocket/1.6.0/Java-WebSocket-1.6.0.jar':'eae29213e4f16515639c28957200f011b3967fffcada1962cf0255d24919c22f',
'org/slf4j/slf4j-api/2.0.16/slf4j-api-2.0.16.jar':'a12578dde1ba00bd9b816d388a0b879928d00bab3c83c240f7013bf4196c579a',
'org/slf4j/slf4j-nop/2.0.16/slf4j-nop-2.0.16.jar':'deca6c04ed35515a0a911fa44c0e836bee92c0c59d2e8fa9bab8ffbc464a9ba7',
'org/msgpack/msgpack-core/0.9.10/msgpack-core-0.9.10.jar':'a9a3d6702661c24c08fd750c4ba81e9eb764b52188326bb0b66fc6419a282687'}
for path, expected in pins.items():
    dest=root/Path(path).name
    if dest.is_file() and hashlib.sha256(dest.read_bytes()).hexdigest()==expected: continue
    with urllib.request.urlopen('https://repo.maven.apache.org/maven2/'+path,timeout=20) as reply: data=reply.read()
    if hashlib.sha256(data).hexdigest()!=expected: raise SystemExit('Hash mismatch: '+dest.name)
    dest.write_bytes(data)
