.. zephyr:board:: mt8370_genio_510_evk

.. Outstanding before this page can be submitted upstream. Every **TODO(n)**
   marker below must be resolved and this comment block removed.

   TODO(1)  Board photo: doc/img/mt8370_genio_510_evk.webp
            WebP format, at most 600 px on its largest dimension, transparent
            background preferred so it works with both doc themes.
   TODO(2)  DONE - 2x Cortex-A78 @ 2.0 GHz, 4x Cortex-A55 @ 2.0 GHz.
   TODO(3)  Board hardware: DRAM size and type, storage, wireless, connectors,
            LEDs, debug interfaces.
   TODO(4)  Which physical connector and pins carry UART1 on this board.
   TODO(5)  Confirm the Jailhouse cell filenames for the Genio 510 EVK. The
            names below are taken from the internal Yocto/Jailhouse notes and
            have not been checked against a shipping image.
   TODO(6)  Whether the root cell is enabled automatically by the image, or has
            to be enabled by hand as shown.
   TODO(7)  Where a reader obtains a Jailhouse-enabled IoT Yocto image, and the
            IP address or connection method for the target.
   TODO(8)  Product page and hardware documentation URLs for the References
            section.

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

**TODO(3):** board hardware summary - DRAM, storage, wireless, USB, Ethernet,
connectors, LEDs and debug interfaces.

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

**TODO(4):** which physical connector and pins expose UART1 on this board.

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

**TODO(8):** product page and hardware documentation URLs.
