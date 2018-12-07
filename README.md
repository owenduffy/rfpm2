# Radio Frequency Power Meter 2
Repository for code related to project described at See https://owenduffy.net/blog/?s=rfpm2

In addition to libraries available under Arduino Library Manager, the project uses the following:
* [NewLiquidCrystal](https://bitbucket.org/fmalpartida/new-liquidcrystal/wiki/Home)
* [LcdBarGraphX (modified)](https://github.com/owenduffy/LcdBarGraphX)
 
See discussion about [I2C LCD configuration](https://owenduffy.net/blog/?s=LiquidCrystal_I2C+type).

# A word about 5V tolerance.

The specs for the ESP9266 do NOT state that they are 5V tolerant logic pins. Further, the maker states in a blog that they are not.

Nevertheless, many users insist that they are, and it seems that they are certainly tolerant so long as the current driven into an IO pin is low.

My implementation uses a 5V display, and the PCF8574T I2C expander also runs on 5V. The I2C bus pins should never be used with active pull up, so they should never drive high current into other pins on the bus. The expander
board has 10k resistor pull resistors up to +5V, so that limits the current that might flow into a 3.3V IO chip pin on the I2C bus (ie the ESP8266) to about 0.2mA.

So whilst not a textbook 5V tolerant design, there is good reason to think that the implementation does not stress the ESP8266 pins, and I have used this on a number of projects and they have been fine.

The preference would be to use a 3V LCD, but the one I used in this project was originally premised on using a 5V AVR chip (the concept was laid down years ago, and the box fitted up with a display). I cannot
find a 3V display with the same mounting holes... so it is what it is.

But if you are starting fresh, get a 3V display... but mind the capacity of the on-board 3V regulator... you may need a separate regulator for the LCD to avoid overheating the ESP8266 board.

# USB chip
The prototype used a nodeMCU v1.0 dev board with CP210x USB chip. The boards are available with other USB chips, but I would avoid ANY Prolific chips and CH340 chips for driver compatibility reasons.
 FTDI spoiled their reputation by disabling clones and there are lots of cloned FTDI chips out of China that make them a risky future.

Copyright: Owen Duffy 2018/04/07.


