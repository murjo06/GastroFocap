## Gastro Focap communication protocol

The protocol is split into two "parts", one for the flatcap and one for the focuser. The flatcap protocol is based on the Alnitak flip-flat protocol, while the focuser is based on the Moonlite focuser. To differenciate between the flatcap and focuser commands, the protocol uses the char `>` as a begin condition for the flatcap and the char `:` for the focuser. The terminating character is `#` for both.

#### Commands for the flatcap:

| Driver request, firmware response		| Explenation
| :-									| :-
| >P000#, *Pid000#						| ping, confirm
| >S000#, *Sid000#						| request state, returned state
| >O000#, *Oid000#						| unpark shutter, confirm
| >C000#, *Cid000#						| park shutter, confirm
| >L000#, *Lid000#						| turn light on (uses set brightness value), confirm
| >D000#, *Did000#						| turn light off (brightness value should not be changed), confirm
| >Bxxx#, *Bidxxx#						| set brightness to xxx, confirm
| >Zxxx#, *Zidxxx#						| set parked angle to xxx, confirm
| >Axxx#, *Aidxxx#						| set unpark angle to xxx, confirm
| >J000#, *Bidxxx#						| get brightness, returned brightness
| >Hxxx#, *Hidxxx#						| get park angle, returned angle
| >Kxxx#, *Kidxxx#						| get unpark angle, returned angle
| >Vxxx#, *Vidxxx#						| get firmware version, returned firmware verison

#### Commands for the focuser:

| Driver request						| Explenation
| :-									| :-
| :PH#									| home motor
| :GV#									| firmware version
| :C#									| begin temperature conversion
| :GP#									| get motor position
| :GN#									| get target position
| :GT#									| get temperature
| :GC#									| get temperature coefficient
| :SC#									| set temperature coefficient
| :GI#									| get motor status (01 moving, 00 still)
| :SP#									| set current motor position
| :SN#									| set new motor position
| :FG#									| initiate move
| :FQ#									| abort motion