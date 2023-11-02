# Agon legacy firmware update utility
The Agon firmware update utility is able to flash MOS a file on the SD card on any Agon running MOS versions 1.00/1.01/1.02, without using any cables.

### Installation
Place these files in the **root** directory of the microSD card:
- The legacy [flash.bin](https://github.com/envenomator/agon-flashlegacy/blob/master/binaries/flash.bin) utility
- The required MOS firmware file - MOS.bin

### Usage

Load and Jump to the binary in memory:
```console
LOAD flash.bin
JMP &040000
```

## Disclaimer
Having subjected my own gear to this utility many hundreds of times, I feel it is safe for general use in upgrading the MOS firmware to a new version.
The responsibility for any issues during and/or after the firmware upgrade, lies with the user of this utility.

## If all else fails
Did you also update your VDP firmware, after updating the MOS? Weird things may happen if both are far apart.

And even then; stuff breaks, things happen. Yeah, wonderful, but what if after whatever happened, you have bricked your AgonLight? Take a deep breath, and have a look at [this](https://github.com/envenomator/agon-vdpflash) utility to perform a *baremetal* recovery.
