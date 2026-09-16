.. _nrf9251_smp_single_slot_sample:

.. ncs-sample::
   :title: nRF9251 SMP single-slot DFU

   This sample demonstrates single-slot firmware update on the nRF9251 DK using MCUboot serial recovery.
   A host MCU (or a PC) transfers the signed application image over UART SMP while MCUboot runs the MCUmgr server.

Requirements
************

The sample supports the following development kit:

.. table-from-sample-yaml::

You need:

* An SMP client such as the `mcumgr`_ CLI or ``nrfutil mcu-manager serial``.
* A serial link to **uart132** for the update (see :ref:`nrf9251_smp_single_slot_uart`).
  The DK USB virtual COM port is usually **uart133** (console only).

Overview
********

With :kconfig:option:`CONFIG_SINGLE_APPLICATION_SLOT`, only ``cpuapp_slot0_partition`` holds the application.
The application cannot receive an image that overwrites itself, so **firmware update is performed in MCUboot serial recovery mode**, not in the application.

The application still runs a minimal SMP server (MCUmgr ``os`` group only) so a host can request recovery using an MCUmgr reset with the boot mode parameter while the application is running.
Image upload uses MCUboot on the same UART.

.. _nrf9251_smp_single_slot_uart:

UART assignment on the nRF9251 DK
=================================

MCUboot and the application must not share the same UART for SMP and console in the MCUboot image.
This sample points MCUmgr at ``uart132`` and leaves the console on ``uart133``.

.. list-table::
   :header-rows: 1

   * - UART
     - Role
     - Host connection
   * - ``uart132``
     - SMP (MCUmgr)
     - **Required for DFU** — TX ``P2.05``, RX ``P2.04``, 115200 8N1
   * - ``uart133``
     - Console
     - Default DK VCOM — application and MCUboot logs

If you only use the DK USB cable, you typically see **uart133** only.
Connect a USB-UART adapter to ``P2.04``/``P2.05`` to run the update from a PC.

Building and running
********************

.. |sample path| replace:: :file:`samples/dfu/nrf9251_smp_single_slot`

.. include:: /includes/build_and_run.txt

The sample must be built with sysbuild.
From your |NCS| workspace:

.. code-block:: console

   west build -p -b nrf9251dk/nrf9251/cpuapp --sysbuild nrf/samples/dfu/nrf9251_smp_single_slot

Sysbuild produces three images: ``mcuboot``, ``uicr``, and the application.

Program the device (use erase on first programming):

.. code-block:: console

   west flash --erase

Testing
=======

The following procedure performs an end-to-end update over SMP.

#. Build and flash the sample as described above.
#. Open a serial terminal on the **console** (**uart133**, usually the DK USB VCOM) at **115200 8N1**.
#. Reset the DK and confirm the log line::

      nRF9251 SMP single-slot sample, firmware version 1

   LED0 blinks at a rate that matches the version (version ``N`` gives about ``N`` Hz).
#. Edit :file:`src/main.c` and increase ``APP_FW_VERSION`` (for example, from ``1`` to ``2``).
#. Rebuild the sample (omit ``-p`` if you only changed the application source)::

      west build -b nrf9251dk/nrf9251/cpuapp --sysbuild nrf/samples/dfu/nrf9251_smp_single_slot

   The file to upload is::

      build/nrf9251_smp_single_slot/zephyr/zephyr.signed.bin

#. Connect the host to **uart132** (see :ref:`nrf9251_smp_single_slot_uart`).
   Do **not** use the console VCOM for upload unless it is wired to ``uart132``.
#. Enter MCUboot serial recovery using one of these methods:

   * Hold **Button 0** (``P0.01``) and press **RESET**, then release the button.
   * Within **5 seconds** after reset, send any MCUmgr frame on **uart132**
     (:kconfig:option:`CONFIG_BOOT_SERIAL_WAIT_FOR_DFU`).
   * From the running application, send MCUmgr ``os`` reset with ``boot_mode`` set to enter the bootloader
     (retained RAM via :file:`dts/boot_mode.dtsi`; see also :ref:`mcuboot_serial_recovery`).

   On the console, MCUboot should remain in serial recovery instead of booting the application.
#. Upload the new signed application on **uart132**.

   Using `mcumgr`_:

   .. code-block:: console

      export PORT=/dev/ttyUSB0

      mcumgr --conntype serial --connstring "dev=$PORT,baud=115200" \
        image upload build/nrf9251_smp_single_slot/zephyr/zephyr.signed.bin
      mcumgr --conntype serial --connstring "dev=$PORT,baud=115200" reset

   Using ``nrfutil mcu-manager serial``:

   .. code-block:: console

      nrfutil mcu-manager serial image-upload --serial-port $PORT --image-number 0 \
        --firmware build/nrf9251_smp_single_slot/zephyr/zephyr.signed.bin
      nrfutil mcu-manager serial reset --serial-port $PORT

   Replace ``$PORT`` with the device node for **uart132**.
   There is no ``image confirm`` step in single-slot mode.

#. Reset if needed and check the **console** for the new firmware version and faster LED blink.

Troubleshooting
---------------

.. list-table::
   :header-rows: 1

   * - Symptom
     - Likely cause
   * - Upload has no effect
     - Wrong serial port — SMP must be on **uart132**, not the console on **uart133**
   * - Application boots immediately
     - Serial recovery not entered — retry button + reset or the 5 s MCUmgr window
   * - Upload succeeds but version unchanged
     - Wrong ``zephyr.signed.bin`` path, or application not rebuilt after changing ``APP_FW_VERSION``
   * - No second serial port on the host
     - Add a USB-UART on ``P2.04`` (RX) and ``P2.05`` (TX)

.. note::
   On nRF92, peripheral pin configuration is provisioned in UICR (PERIPHCONF) and is not updated by a slot0-only SMP upload.
   Plan peripheral usage before deployment, or use :kconfig:option:`SB_CONFIG_NCS_MCUBOOT_LOAD_PERIPHCONF` for image-carried PERIPHCONF (experimental).

Dependencies
************

* :ref:`MCUboot <mcuboot_index_ncs>`
* :ref:`device_mgmt`

.. _mcumgr: https://github.com/apache/mynewt-mcumgr-cli
