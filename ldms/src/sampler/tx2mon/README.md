# Marvell TX2 plug-in.

## Introduction
The tx2mon sampler plug-in reports the same information as the
tx2mon command line tool:
```
Node: 0  Snapshot: 40146152
Freq (Min/Max): 1000/2000 MHz     Temp Thresh (Soft/Max):  92.39/110.25 C

|Core  Temp   Freq |Core  Temp   Freq |Core  Temp   Freq |Core  Temp   Freq |
+------------------+------------------+------------------+------------------+
|  0:  58.89  2001 |  1:  57.78  2001 |  2:  57.78  2004 |  3:  59.45  2003 |
|  4:  58.34  2002 |  5:  57.78  2005 |  6:  56.10  2006 |  7:  57.78  2001 |
|  8:  60.01  2007 |  9:  56.10  2003 | 10:  59.45  2006 | 11:  57.22  2000 |
| 12:  58.89  2002 | 13:  56.66  2004 | 14:  60.01  2000 | 15:  58.89  2007 |
| 16:  56.10  2006 | 17:  58.34  2001 | 18:  58.89  2006 | 19:  57.78  2006 |
| 20:  58.34  2001 | 21:  58.89  2003 | 22:  58.34  2000 | 23:  57.22  2003 |
| 24:  56.10  2003 | 25:  60.01  2005 | 26:  57.78  2000 | 27:  56.66  2006 |

SOC Center Temp:  60.57 C
Voltage    Core:   0.74 V, SRAM:  0.90 V,  Mem:  0.93 V, SOC:  0.85 V
Power      Core:  18.97 W, SRAM:  0.45 W,  Mem: 13.41 W, SOC: 15.21 W
Frequency    Memnet: 2304 MHz


Throttling Active Events: None
Throttle Events     Temp:      0,    Power:    108,    External:      0
Throttle Durations  Temp:      0 ms, Power:  56220 ms, External:      0 ms

Node: 1  Snapshot: 40026090
Freq (Min/Max): 1000/2000 MHz     Temp Thresh (Soft/Max):  92.39/110.25 C

|Core  Temp   Freq |Core  Temp   Freq |Core  Temp   Freq |Core  Temp   Freq |
+------------------+------------------+------------------+------------------+
|  0:  50.52  2006 |  1:  51.64  2006 |  2:  49.41  2003 |  3:  49.41  2001 |
|  4:  52.75  2001 |  5:  52.20  2006 |  6:  50.52  2002 |  7:  51.08  2001 |
|  8:  52.20  2000 |  9:  52.20  2005 | 10:  51.64  2000 | 11:  49.41  2002 |
| 12:  51.64  2005 | 13:  49.96  2004 | 14:  53.31  2001 | 15:  49.41  2000 |
| 16:  52.20  2004 | 17:  53.31  2005 | 18:  51.64  2002 | 19:  54.43  2000 |
| 20:  51.64  2005 | 21:  51.64  2001 | 22:  50.52  2003 | 23:  49.96  2003 |
| 24:  50.52  2001 | 25:  49.96  2000 | 26:  52.75  2005 | 27:  49.41  2006 |

SOC Center Temp:  51.64 C
Voltage    Core:   0.74 V, SRAM:  0.90 V,  Mem:  0.93 V, SOC:  0.85 V
Power      Core:  19.69 W, SRAM:  0.54 W,  Mem: 14.30 W, SOC: 14.54 W
Frequency    Memnet: 2304 MHz


Throttling Active Events: None
Throttle Events     Temp:      0,    Power:     91,    External:      0
Throttle Durations  Temp:      0 ms, Power:  60362 ms, External:      0 ms
```
The tx2mon code is available [here](https://github.com/jchandra-cavm/tx2mon),
it contains two components:
* kernel module

  This makes the data structures from the M3 manangement processor on each TX2 die available in sysfs.
  The tx2mon\_kmod kernel module must be present for the sampler to operate.

* user application

  This mmaps the sysfs files, and displays their contents in real time.

This sampler requires the kernel module to be loaded.
## Building
In order to build the tx2mon sampler, the Marvell tx2mon header needs to be resident on the build system.
By default, the plugin is enabled if the header is present and disabled if it is not found automatically.

