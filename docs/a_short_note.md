## My Setup for performing Serial Wire Debug Operations with the PineTime


I opted to use a slightly different setup for performing SWD communication
with my PineTime smart watch than most people use.
I use a RasberryPi-2 (RBPI) as an IO board for doing the communication, but I use one of the "auxiliary" SPI
interfaces provided by the BCM2385/RBPI.
The auxiliary interface has the benefit of allowing arbitrary length SPI words to be sent & received.
This is helpful for performing SWD operations which require you to send an 8-bit header, then receive a 3-bit
acknowledge, then send/receive a 32-bit data word.
The fact that the data direction switches on non-byte aligned positions means arbitrary length operations is
useful.

The downside of auxiliary SPI interface is that it has two separate pins for receiving (MISO) and transmitting (MOSI) data,
unlike the primary SPI interface which optionally can use a single pin for both input and output (MOMI).
This will not work with the PineTime's SWD interface at all; the PineTime uses 2-wires for performing SWD operations
a CLK and a bi-directional data pin.
I was able to mitigate the issue presented by the auxiliary SPI interfaces separate MISO and MOSI pins by connecting the
two pins with a 1k Ohm resistor, then I connected the PineTime's SWD data port to the MISO pin of the RBPI.
With that resistor every bit set along the MOSI pin is also received by the MISO pin and the PineTime. Except for when
the PineTime sends bits back to the RBPI.
In that case the MISO pin is set to whatever the PineTime is sending and the MOSI pin simply acts as a pull-up/pull-down.
In my code I ensure that it always acts as a pull-up.
This setup also means the code responsible for interpreting data from the MISO pin must be careful to only use the bits where
the PineTime ~should~ be controlling the line.

The second downside of using the auxiliary SPI interface is that it seems to be only rarely used by people in general.
There's relatively little documentation for how to use the auxiliary SPI interfaces compared to the primary SPI interface,
and the BCM2385 C library provides only a modest amount of functionality for it.
Despite this, controlling the auxiliary SPI interface is not difficult.
The BCM2385 C library provides enough code to access all the registers related to the interface and with that its just
a matter of writing to a handful of registers to send/receive bits along the wire.

The code for initializing the RBPI memory map and then setting up the auxiliary SPI interface can be seen
[here](https://github.com/marzece/raspberry_pine/blob/master/rbpi.c#L191) and [here](https://github.com/marzece/raspberry_pine/blob/master/rbpi.c#L128).
Much the code shown there was adapted from similar code within the BCM2385 library.
Once the memory map has been created and the SPI pins have been initialized, a few control and config
registers need to be setup so that the auxiliary SPI interface is enabled and will behave as desired;
the code that does that setup can be seen [here](https://github.com/marzece/raspberry_pine/blob/master/test_mem.c#L176).
Once the setup is complete IO operations can take place. 
