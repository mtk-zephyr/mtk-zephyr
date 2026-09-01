.. zephyr:board:: mt8370_genio_510_evk

.. Outstanding before this page can be submitted upstream. Resolve every
   numbered placeholder in the body, then delete this comment block.

   [5] Confirm the Jailhouse cell filenames. The names in the body are taken
       from the internal Yocto/Jailhouse notes and have not been checked
       against a shipping image.
   [6] Whether the root cell is enabled automatically by the image, or has to
       be enabled by hand as shown.
   [7] Where a reader obtains a Jailhouse-enabled IoT Yocto image, and the IP
       address or connection method for the target.

Overview
********

The MediaTek Genio 510 EVK is an evaluation board for the Genio 510 (MT8370)
application processor, aimed at edge AI and IoT designs.

The MT8370 is a derivative of the same die as the MT8390, which is identified
internally as MT8188. MT8188 is the name this port uses for the SoC, its drivers
and its devicetree, and both Genio boards target ``mt8188/a55``; ``mt8370``
appears only in the board name.

The MT8370 combines two Cortex-A78 cores at 2.0 GHz with four Cortex-A55 cores
at 2.0 GHz, an Arm Mali-G57 MC2 GPU, a 3.2 TOPS NPU and a Tensilica HiFi 5 audio
DSP. MediaTek describes it as pin-to-pin and software compatible with the
higher-performance Genio 700, which is why both boards share this port's SoC,
drivers and devicetree.

Zephyr runs on **one Cortex-A55 core** of this board, as a Jailhouse inmate
alongside Linux. See `Programming and Debugging`_.

Hardware
********

- MediaTek MT8370 (Genio 510)
- 4 GB LPDDR4X DRAM
- 64 GB eMMC 5.1 and a microSD card slot
- AzureWave AW-XB468NF Wi-Fi module (MT7921)
- 10/100/1000M Ethernet
- One USB Type-C host port and one micro USB device port
- Two M.2 slots (PCIe/USB and SDIO)
- 40-pin 2.54 mm expansion header with a Raspberry Pi compatible pinout
- Three micro USB connectors carrying UART trace logs through USB-to-UART
  bridges
- Power, reset and system LEDs; power, reset, download and home buttons
- 12 V DC input on a 2.0 mm jack

Supported Features
==================

.. zephyr:board-supported-hw::

Zephyr supports the peripherals that are handed to the inmate cell. Everything
else on the board stays with the Linux root cell and is left disabled in the
board devicetree.

Connections and IOs
===================

Serial Port
-----------

The Zephyr console is on **UART1** at 115200 8N1, muxed onto pins 33 (UTXD1)
and 34 (URXD1).

The board brings out three UARTs, each through a USB-to-UART bridge on its own
micro USB connector:

======  ==========
UART    Connector
======  ==========
UART0   CN3200
UART1   CN3201
UART2   CN3202
======  ==========

UART0 is the Linux console and stays with the root cell, so Zephyr uses UART1
on **CN3201**.

Programming and Debugging
*************************

Zephyr does not run on this board bare metal. Linux owns the board, and the
`Jailhouse`_ hypervisor is used to take one Cortex-A55 core away from Linux and
hand it to Zephyr.

.. _Jailhouse:
   https://github.com/siemens/jailhouse

Three Jailhouse concepts matter here:

* A **cell** is a set of hardware resources assigned to one operating system.

* The **root cell** is the cell Linux runs in. It holds every resource Linux
  uses, and resources are assigned to inmates out of it.

* An **inmate** is any other operating system running alongside Linux. Zephyr
  is an inmate here.

Neither Linux nor Zephyr is aware of the other. Each sees only the cores and
memory its cell describes, and the hypervisor enforces that with second-stage
translation. A resource absent from the inmate's cell configuration faults if
the inmate touches it.

Zephyr is given one Cortex-A55 core and a 2 MB window of DRAM at physical
address ``0x8000``. The other cores and all remaining peripherals stay with
Linux.

Prerequisites
=============

A Linux image on the target with Jailhouse support built in, including the cell
configuration files for this board.

**TODO(7):** where the reader obtains such an image, and how to reach the
target.

Building
========

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :host-os: unix
   :board: mt8370_genio_510_evk/mt8188/a55
   :goals: build

The artefact to load is the raw binary ``build/zephyr/zephyr.bin``, not the ELF.
Copy it to the target, for example with ``scp``.

Loading
=======

On the target, with the Zephyr binary in the current directory:

.. code-block:: console

   modprobe jailhouse
   jailhouse enable      /usr/share/jailhouse/cells/genio-510-evk.cell
   jailhouse cell create /usr/share/jailhouse/genio-510-evk-zephyr.cell
   jailhouse cell load   zephyr zephyr.bin -a 0x8000
   jailhouse cell start  zephyr

**TODO(5):** confirm those two cell filenames against a shipping image.

**TODO(6):** whether ``jailhouse enable`` is needed, or the image brings the
root cell up on boot.

.. important::

   The load address passed to ``jailhouse cell load`` must be ``0x8000``. It has
   to match both the inmate base address the hypervisor was configured with and
   the ``memory@8000`` node in the board devicetree. A mismatch places the image
   somewhere Zephyr is not linked to run, and nothing in the build catches it.

Output appears on the console described in `Connections and IOs`_:

.. code-block:: console

   *** Booting Zephyr OS build v4.4.0 ***
   Hello World! mt8370_genio_510_evk/mt8188/a55

To stop and unload the inmate:

.. code-block:: console

   jailhouse cell shutdown zephyr
   jailhouse cell destroy  zephyr

Debugging
=========

No Zephyr flash or debug runner is provided: the image is loaded by the
hypervisor from Linux rather than by a host-side tool, so ``west flash`` and
``west debug`` do not apply to this board.

References
**********

Genio 510 product page:
    https://genio.mediatek.com/genio-510

Genio 510 EVK hardware documentation:
    https://genio.mediatek.com/doc/iot-yocto/latest/hw/g510-evk.html

IoT Yocto documentation:
    https://genio.mediatek.com/doc/iot-yocto/latest/
