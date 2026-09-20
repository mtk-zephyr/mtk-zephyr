.. zephyr:board:: mt8370_genio_510_evk

Overview
********

The `MediaTek Genio 510 EVK`_ is an evaluation board for the Genio 510
(MT8370) application processor, aimed at edge AI and IoT applications.

.. _MediaTek Genio 510 EVK:
   https://genio.mediatek.com/doc/iot-yocto/latest/hw/g510-evk.html

MT8370 is the SoC powering Genio 510 platform. The SoC IPs are based on
MT8188 drivers and device trees, MT8370 appears only in the board name.

The MT8370 combines two Cortex-A78 cores at 2.0 GHz with four Cortex-A55 cores
at 2.0 GHz, an Arm Mali-G57 MC2 GPU, a 3.2 TOPS NPU and a Tensilica HiFi 5 audio
DSP. MediaTek describes it as pin-to-pin and software compatible with the
higher-performance Genio 700, which is why both boards share this port's SoC,
drivers and devicetree.

Zephyr runs on **one Cortex-A55 core** of this board, as a Jailhouse inmate
alongside Linux. See `Programming and Debugging`_.

Hardware
********

- MediaTek Genio 510 EVK
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

GPIO
----

The inmate cell is given two pins, **GPIO 38** and **GPIO 40**. They are pins 6
and 8 of GPIO bank 1, which covers GPIO 32 to 63:

.. code-block:: c

   const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio32_63));

   gpio_pin_configure(gpio, 6, GPIO_OUTPUT_INACTIVE);  /* GPIO 38 */
   gpio_pin_configure(gpio, 8, GPIO_INPUT);            /* GPIO 40 */

Every other pin of the bank stays with the Linux root cell and is listed in the
bank's ``gpio-reserved-ranges``, so the driver rejects it; the five remaining
banks are disabled for the same reason. Both usable pins carry JTAG signals in
every function other than GPIO, so the board selects the GPIO function for them
in its pin control state.

Both are brought out on the 40-pin Raspberry Pi HAT header:

=========  ==================  ==================
Bank pin   SoC GPIO            Header pin
=========  ==================  ==================
6          GPIO 38             22
8          GPIO 40             18
=========  ==================  ==================

.. note::

   Header pins 18 and 22 are both on the even-numbered row with **pin 20, a
   ground pin, between them**. A two-position jumper block cannot bridge them,
   and one fitted across 18-20 or 20-22 would tie a usable pin to ground. The
   loopback needs a wire.

Pin interrupts are delivered by the SoC's external interrupt controller. Rising,
falling and both-edge triggers are available. Level triggers are not: the
controller's own output to the GIC is level-triggered, so a level that stays
asserted would re-enter the handler for as long as it lasts.

Audio
-----

The Audio Front End is off by default and enabled with the ``mtk-afe`` snippet,
which turns on the AFE node, selects the eTDM pins and reserves an 8 MB buffer
region at ``0x61000000``:

.. code-block:: console

   west build -b mt8370_genio_510_evk/mt8188/a55 -S mtk-afe <app>

.. important::

   An image built with this snippet needs a Jailhouse cell that grants the AFE.
   The plain ``genio-510-evk-zephyr`` cell grants none of the five register
   blocks the driver touches, and the inmate is stopped on the first access.
   Build without the snippet for that cell.

The snippet is deliberately separate rather than being part of the board, so
the default image keeps working on the plain cell.

This board and the Genio 700 EVK route the audio serial pins identically, so
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
   jailhouse cell create /usr/share/jailhouse/cells/genio-510-evk-zephyr.cell
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
