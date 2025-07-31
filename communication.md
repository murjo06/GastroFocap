| >P000#, *Pid000#	| ping, confirm
| >S000#, *Sid000#	| request state, returned state
| >O000#, *Oid000#	| unpark shutter, confirm
| >C000#, *Cid000#	| park shutter, confirm
| >L000#, *Lid000#	| turn light on (uses set brightness value), confirm
| >D000#, *Did000#	| turn light off (brightness value should not be changed), confirm
| >Bxxx#, *Bidxxx#	| set brightness to xxx, confirm
| >Zxxx#, *Zidxxx#	| set parked angle to xxx, confirm
| >Axxx#, *Aidxxx#	| set unpark angle to xxx, confirm
| >J000#, *Bidxxx#	| get brightness, returned brightness
| >Hxxx#, *Hidxxx#	| get park angle, returned angle
| >Kxxx#, *Kidxxx#	| get unpark angle, returned angle
| >Vxxx#, *Vidxxx#	| get firmware version, returned firmware verison