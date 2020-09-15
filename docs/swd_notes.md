# Notes
I've implemented some code for doing SWD interaction with the NRF52832 on the PineTime using a 
raspberry pi.
I use the first auxillary SPI interface on the raspberry pi to do this, because that interface can
perform arbitrary length SPI transactions. The primary SPI interface must do byte length transfers.
The downside of the auxillary interface is that it cannot do bi-directional data transfers on a single
SPI data pin. To get around this I attached a resistor between the auxillary SPI's MOSI and MISO pins.
Then I attach the SWD data signal to the MISO pin; when the data is going from the RBPI to the NRF that pin
will match the MOSI. When data is going the other way I have the MOSI pin
output a digital HIGH so it acts as a pull up, and the MISO pin will be at whatever value the NRF
specifies.

With that done and tested then the question is about tackling the SWD protocol.
The following images show the SWD packet.

![](resources/swd_read_packet.png)

![](resources/swd_write_packet.png)

The SWD protocol does this thing where if the APnDP bit is 0 you talk to the
DEBUG PORT (DP), if that bit is 1 you talk to the ACCESS PORT (AP).

Here's a table from the ARM SWD spec that shows the registers that live within
the DP.

![](resources/dp_regs.png)

You'll notice that bits 2 and 3 of the addr line are the only ones used to specify the address,
and also the only ones specified in the SWD packet.
The DBBANKSEL value is specified by writing to the SELECT register.
Although I basically never need to use anything other than DPBANKSEL=0.
Each register is nominally 32-bits long, although most of the registers don't really use
all 32-bits.

In general (as far as I can tell) you have to talk to the the DP first and read
the DPIDR reg before doing anything else.
Idk why...but you gotta read that reg.

Below is some copy/paste screenshots from the specification for info on the contents
of each resgister

## DP Regs

### DPIDR

![](resources/dpidr_contents.png)

### CNTRL/STAT

![](resources/cntrlstat_contents.png)

~IMPORTANT~ the various bits of the CNTRL/STAT reg that are blahblah-REQ are
used to power-up elements of the debug interface. Specifically bits 31 and 28
are necessary. 

### SELECT

![](resources/select_contents.png)

### RDBUFF
RDBUFF is just a 32-bit data register that holds the value from the last AP memory
read access.


That basically does it for the DP regs. There exists more that may be at times useful
(like the ABORT reg) but lets not worry about those right now.
Lets move on to the AP regs.
Below is a map of the APs and how they interact with the other NRF elements.

![](resources/NRF_AP_map.png)

The NRF52 has two seperate APs, selected by the value in the APSEL bits of the DP's SELECT
register.
APSEL=1 is the CTRL-AP and is the simpler one, and it is not a "MEM-AP", it just provides info and utilities for
the NRF but not really any access to memory or device.
Here's the regs/bits available in the CNTRL-AP
![](resources/cntrlap_regs.png)

Obviously, important here is the ERASEALL register, which is necessary for getting rid of the 
"factory" firmware and unlocking the other AP.
The rest are more or less not so important, maybe the RESET is important once I want to try
flashing new firmware or something like that.

Perhaps worth mentioning here is that the IDR reg exists at address 0xFC. Since the SWD packet only has
address bits 2 & 3 how do you right 0xF to bits 7-4?
The answer is that the top 4 bits is read from the value in the DP's select register in the APBANKSEL
bits.

The second AP (APSEL=0) is the AHB-AP, it provides access to the main memory bus of the whole
chip.
Importantly it is a MEM-AP, most of the properties of a MEM-AP are specified by the ARM debug
interface.
Firstly, all AP's, MEM-AP or otherwise,  are required to have an ID reg at address 0xFC.
Secondly the MEM-AP interaction protocol/map/thing is mostly described by the following figure

![](resources/mem_ap_map.png)

The most important regs here are the DRW and TAR.
To get values to and from arbitrary memory locations you write the (32-bit) address in the TAR, the
perform either a read or a write to the DRW.
Reading from the DRW will return the value currently in memory at the TAR address, writing to the DRW
will place the written value in memory at the TAR address.
For reading values from the CSW (or any AP memory access) you need to do the read twice, idk why, but there's
always a lag of one value for memory accesses. 

Worth noting is that I have no idea how the "Debug address decode" works for the NRF...
that may or may not be a huge problem...to be determined.

## NRF Memory
I'm not 100% clear on how the memory for the NRF/Pinetime works. This is still a work-in-progress for
me. So this info is just my current understanding.
Here's a memory map from the NRF data sheet.

![](resources/nrf_memory.png)

And here's how memory is used by the CPU

![](resources/nrf_mem_cpu.png)

I BELIEVE the place where my (compiled) code should go is "FLASH".
So, I THINK (THINK!) what I should do is place the binary compiled code is in flash starting at address
0x0 and just going up from there.
I fear there's some need to split instructions and data...idk though dude.
I also fear there's little endian/big endian bullshit that needs to be considered.
(N.b. THE CFG register (addr=0xF4) of the MEM-AP says if big-endian is supported...and it would seem
it is not. So everything is little endian).

I think my plan from here is just squirt the code across the SWD verbatim, starting at 0x0 and going up.
Then doing a RESET (in the CNTRL-AP). Then seeing if things work.
If things work, good...if not I have to do some work.





## Basic steps for getting to memory

1. Perform a JTAG-TO-SWD
2. Perform an SWD RESET
3. Read the DPID
4. Power up the debug bits in the CNTRL/STAT
5. Set the DP's SELECT reg to APSEL=0x1 APBANK=0x0, check the protect status and such
6. Set the DP's SELECT reg to APSEL=0x0 APBANK=0x0
7. Start doing DRW/TAR IO


## Random extemporanius baloney
So doing random IO with the DRW/TAR at or around address 0x0 (which is the flash) shows some unexpected behavior.
First writing (say) a value of 0xABCD to 0x0 then reading the value back gives 0xABFF.
Writing  0xABCD1234 gives 0xABCD12FF...so the bottom bits seem "stuck".
Secondly, doing the above write to address 0x0 then reading back (say) address 0x4 or 0x10 or whatever gives
0xABCD12FF (same value as 0x0).
All the addresses at or around 0x0 read back the same value.
I haven't tested at what address that behavior changes (if it does at all).

Reading the docs for the NRF seems to suggest that non-volatile memory (i.e. flash) is controlled
by the NVMC (non-volatile memory controller). So perhaps it's not as simple as just yeet-ing the code
into the memory where I want it.

Table 6 (page 21) of the NRF62832 datasheet gives the NVMC address as 0x4001E000.

Chapter 11 (page 26) of the NRF datasheet gives info about the NVMC. First it has a 
config register at offset 0x504 (i.e. global addres 0x4001E504)

...Lots of debugging and testing and playing later...
I played around with the NVMC registers quite a bit, erasing chunks of memory and then 
enabling writing and then going to write values at global memory address 0x0.
But always I saw 0xFF in the bottom-8 bits...very perplexing.

So I eventually found out that the MEM-AP's CSW register has a 'size' field,
below are some screenshots.

![](resources/csw_fields.png)
![](resources/csw_size_values.png)

Reading the value in the CSW gives 0x23000040. This gives the size=0x0...aka byte length data transfers.
The NVMC documentation chapter explicitly says "only full 32-bit words can be
written to Flash using the NVMC interface".
So my suspicion is/was that if I modify that size field to 32-bit (aka size = 0b010) then do data transfers
things will work.

Testing this out as follows

1. Enable erase in NVMC config
2. Perform ERASEALL in NVMC
3. Enable WRITE in NVMC config (and disable erase)
4. Read value at TAR=0x0 (returns 0xFFFFFFFF)
5. Write value at TAR=0x0 as 0x0
6. Read value at TAR=0x0 (returns 0x0)
7. Success!

Since this worked, one thing worth noting is that the NVMC documentation says it can only write
like 100 times betweeen erases. I assume this is ~100 times for each register...or else you couldn't
fill up the memory. I don't expect this will be a problem since my basic plan right now is to
nuke the memory (erase all) then write the binary data for the program...then be done. No re-writing
ever necessary.

Second point from the docs that I want to make sure is mentioned. When the non-volatile memory is erased
it's set to all '1' (0xFFFFFFFF). When writing to memory all I can do is set bits to '0'. So (for example)
if I set some register to a value of 0x0 the only way to set any other value in that register is to do an erase
to set it back 0xFFFFFFFF then write the value I want.
Again this should be okay so long as I stick to the pattern of erase then write and never re-write.


