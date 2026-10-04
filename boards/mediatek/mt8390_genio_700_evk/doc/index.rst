.. zephyr:board:: mt8390_genio_700_evk

Overview
********

The `MediaTek Genio 700 EVK`_ is an evaluation board for the Genio 700
(MT8390) application processor, aimed at edge AI and IoT applications.

.. _MediaTek Genio 700 EVK:
   https://genio.mediatek.com/doc/iot-yocto/latest/hw/g700-evk.html

MT8390 is the SoC powering Genio 700 platform. The SoC IPs are based on
MT8188 drivers and device trees, MT8390 appears only in the board name.

The MT8390 is a 6 nm part combining two Cortex-A78 cores at 2.2 GHz with six
Cortex-A55 cores at 2.0 GHz, an Arm Mali-G57 MC3 GPU, a 4.0 TOPS NPU and a
Tensilica HiFi 5 audio DSP.

Zephyr runs on **one Cortex-A55 core** of this board, or on two with the
``smp`` variant, as a Jailhouse inmate alongside Linux. See `Programming and
Debugging`_.

Hardware
********

- MediaTek Genio 700 EVK
- 8 GB LPDDR4X DRAM
- 64 GB eMMC 5.1 and a microSD card slot
- AzureWave AW-XB468NF Wi-Fi module (MT7921)
- 10/100/1000M Ethernet
- One USB Type-C host port and one micro USB device port
- Two M.2 slots (PCIe/USB and SDIO)
- Two 4-lane MIPI CSI camera interfaces
- 40-pin 2.54 mm expansion header with a Raspberry Pi compatible pinout
- Three micro USB connectors carrying UART trace logs through USB-to-UART
  bridges
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

GPIO
----

No GPIO bank is enabled by default, because which pins Zephyr may use is decided
by the Jailhouse cell rather than the board. The plain ``genio-700-evk-zephyr``
cell grants GPIO 38 and GPIO 40, pins 6 and 8 of ``gpio32_63``. An application
enables the bank in an overlay and selects the GPIO function for its pins, as
:zephyr_file:`tests/drivers/gpio/gpio_basic_api/boards/mt8390_genio_700_evk_mt8188_a55.overlay`
does. Pin interrupts come from the SoC's external interrupt controller, edge- or
level-triggered. Under Jailhouse that controller is shared with Linux line by
line, and Zephyr changes only the lines it enables.

Audio
-----

The Audio Front End is off by default and enabled with the ``mtk-afe`` snippet,
which turns on the AFE node, selects the eTDM pins and reserves an 8 MB buffer
region at ``0x61000000``:

.. code-block:: console

   west build -b mt8390_genio_700_evk/mt8188/a55 -S mtk-afe <app>

.. important::

   An image built with this snippet needs a Jailhouse cell that grants the AFE.
   The ``genio-700-evk-zephyr-afe`` cell does, and ``genio-700-evk-zephyr-afe-smp``
   for the ``smp`` variant; the plain ``genio-700-evk-zephyr`` cell grants none of
   the five register blocks the driver touches, and the inmate is stopped on the
   first access. Build without the snippet for that cell.

The snippet is deliberately separate rather than being part of the board, so
the default image keeps working on the plain cell.

This board and the Genio 510 EVK route the audio serial pins identically, so
both take their eTDM pin control state from
:zephyr_file:`boards/mediatek/common/genio-evk-pinctrl-common.dtsi`. The ports
reach the following pins:

=========  ===================  ===================
Port       Signals              Pins
=========  ===================  ===================
eTDM_IN1   MCK, BCK, LRCK, DI   125, 126, 127, 128
eTDM_IN2   MCK, BCK, WS, D0     107, 108, 109, 110
eTDM_OUT1  MCK, BCK, WS, D0     4, 5, 6, 11
eTDM_OUT2  MCK, BCK, WS, D0     114, 115, 116, 117
=========  ===================  ===================

The interface is in :zephyr_file:`include/zephyr/drivers/audio/mt8188_afe.h`.
It is not the Zephyr DAI interface: the routing matrix, the channel-merge units
and the co-clocked port pairs have no expression in DAI.

Programming and Debugging
*************************

Zephyr runs on this board using the `Jailhouse`_ hypervisor, where this
hypervisor takes away one Cortex-A55 core from Linux and hands it to Zephyr.

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

Zephyr is given one Cortex-A55 core and an 8 MB window of DRAM, which the cell
places at ``0x8000`` in the inmate's own address space. The other cores and all
remaining peripherals stay with Linux.

Prerequisites
=============

An `IoT Yocto <https://genio.mediatek.com/doc/iot-yocto/latest/>`_ image on the
target. Jailhouse, its kernel module and the cell configurations for this board
are all part of that image, so no separate build is needed.

The instructions below were verified against IoT Yocto ``26.0-release``, running
Jailhouse 0.12 on a 6.6 kernel.

Building
========

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :host-os: unix
   :board: mt8390_genio_700_evk/mt8188/a55
   :goals: build

The artefact to load is the raw binary ``build/zephyr/zephyr.bin``, not the ELF.
Copy it to the target, for example with ``scp``.

Loading
=======

On the target, with the Zephyr binary in the current directory:

.. code-block:: console

   modprobe jailhouse
   jailhouse enable      /usr/share/jailhouse/cells/genio-700-evk.cell
   jailhouse cell create /usr/share/jailhouse/cells/genio-700-evk-zephyr.cell
   jailhouse cell load   zephyr zephyr.bin -a 0x8000
   jailhouse cell start  zephyr

The image does not start the hypervisor by itself, so ``jailhouse enable`` is
needed once after each boot of the target before any inmate can be created.
Subsequent runs need only the last three commands.

.. important::

   The load address passed to ``jailhouse cell load`` must be ``0x8000``. It has
   to match both the inmate base address the hypervisor was configured with and
   the ``memory@8000`` node in the board devicetree. A mismatch places the image
   somewhere Zephyr is not linked to run, and nothing in the build catches it.

Output appears on the console described in `Connections and IOs`_:

.. code-block:: console

   *** Booting Zephyr OS build v4.4.0 ***
   Hello World! mt8390_genio_700_evk/mt8188/a55

Running on a Cortex-A78 core
----------------------------

A single-core image does not depend on the core it runs on, so the image
built for ``mt8390_genio_700_evk/mt8188/a55`` also runs in the
``genio-700-evk-zephyr-a78`` cell, on the Cortex-A78 CPU 7. The ``a55`` in
the board target names the cluster the build is tuned for, not a requirement.
Create ``genio-700-evk-zephyr-a78.cell`` instead of
``genio-700-evk-zephyr.cell`` when loading it.

Running on two cores
--------------------

The ``smp`` variant runs Zephyr's SMP kernel on two Cortex-A55 cores, CPU 2 and
CPU 3, under the ``genio-700-evk-zephyr-smp`` cell:

.. zephyr-app-commands::
   :zephyr-app: samples/arch/smp/pi
   :host-os: unix
   :board: mt8390_genio_700_evk/mt8188/a55/smp
   :goals: build

Load it as above, creating ``genio-700-evk-zephyr-smp.cell`` instead of
``genio-700-evk-zephyr.cell``. The cell starts CPU 2 at ``0x8000``, and Zephyr
starts CPU 3 with PSCI ``CPU_ON``. The hypervisor accepts PSCI calls only by
SMC, which is how the SoC devicetree calls the firmware; an HVC that is not a
hypervisor call stops the cell. The cell grants the same
memory window, console and GPIO pins as the plain one. For audio, build with
the ``mtk-afe`` snippet as well and create ``genio-700-evk-zephyr-afe-smp.cell``,
which also grants the AFE.

.. important::

   The SMP cells come from the ``mtk-genio-dev`` branch of `mtk-jailhouse`_,
   not from the IoT Yocto image, and the hypervisor must include that branch's
   commit "arm-common: gic-v3: Emulate the pending state of SGIs". Without it an
   SMP image can deadlock at start-up: while waiting for a spinlock, Zephyr
   polls the pending state of an SGI from the other core, which an older
   hypervisor never reports as pending.

.. _mtk-jailhouse:
   https://github.com/mtk-jailhouse/jailhouse

If the image and the cell do not agree on the cores, the image fails, and in one
case without a word:

* An ``smp`` image started on a core it does not expect, for example in the
  ``genio-700-evk-zephyr-a78`` cell (MPIDR ``0x700``), stops in its first
  instructions. The boot code looks the core up among the enabled cpu nodes,
  finds none and waits forever, before the console is set up. The cell reports
  ``running`` and nothing appears on the console, which looks exactly like a
  dead serial capture.
* An ``smp`` image in a one-core cell, for example ``genio-700-evk-zephyr``,
  boots on CPU 3, asks the hypervisor to start CPU 2, is refused, and stops
  after printing ``Failed to boot secondary CPU core 1 (MPID:0x200)``.

The variant enables CPU 2 and CPU 3 because those are the cores the SMP cells
grant. For another core set, for example three cores or the two A78 cores, a
cell that grants them is needed, and on the Zephyr side either another variant
or an application overlay that enables the cores, with ``CONFIG_SMP=y``,
``CONFIG_MP_MAX_NUM_CPUS`` and ``CONFIG_PM_CPU_OPS=y`` in the application's
configuration.

An application's board overlay names the full board target, so an overlay
written for ``mt8390_genio_700_evk/mt8188/a55`` is not applied to the ``smp``
variant. Add one for the variant that includes it.

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

Genio 700 product page:
    https://genio.mediatek.com/genio-700

Genio 700 EVK hardware documentation:
    https://genio.mediatek.com/doc/iot-yocto/latest/hw/g700-evk.html

IoT Yocto documentation:
    https://genio.mediatek.com/doc/iot-yocto/latest/
