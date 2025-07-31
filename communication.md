Arduino Nano firmware using Alnitak protocol

Code adapted from (here)[https://github.com/jwellman80/ArduinoLightbox/blob/master/LEDLightBoxAlnitak.ino]

The host (INDI server) sends commands starting with >, this firmware responds with *

| Send		| >P000#	| ping
Recieve		*Pid000#	confirm

| Send		| >S000#	| request state
Recieve		*Sid000#	returned state

| Send		| >O000#	| unpark shutter
| Recieve	| *Oid000#	| confirm

| Send		| >C000#	| park shutter
| Recieve	| *Cid000#	| confirm

| Send		| >L000#	| turn light on (uses set brightness value)
| Recieve	| *Lid000<	| confirm
| :-		| :-
| Send		| >D000#	| turn light off (brightness value should not be changed)
| Recieve	| *Did000<	| confirm
| :-		| :-
| Send		| >Bxxx#	| set brightness to xxx
| Recieve	| *Bidxxx<	| confirm
| :-		| :-
| Send		| >Zxxx#	| set parked angle to xxx
| Recieve	| *Zidxxx<	| confirm
| :-
| Send		| >Axxx#	| set unpark angle to xxx
| Recieve	| *Aidxxx<	| confirm
| :-
| Send		| >J000#	| get brightness
| Recieve	| *Bidxxx<	| returned brightness
| :-
| Send		| >Hxxx#	| get park angle
| Recieve	| *Hidxxx<	| returned angle
| :-
| Send		| >Kxxx#	| get unpark angle
| Recieve	| *Kidxxx<	| returned angle
| :-
| Send		| >Vxxx#	| get firmware version
| Recieve	| *Vidxxx<	| returned firmware verison