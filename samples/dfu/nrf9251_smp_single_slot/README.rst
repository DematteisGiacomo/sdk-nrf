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
* ``nrfutil`` for key provisioning (``device x-provision-keys``).
* Your |NCS| toolchain (``west``, ``imgtool`` via MCUboot).

Overview
********

With :kconfig:option:`CONFIG_SINGLE_APPLICATION_SLOT`, only ``cpuapp_slot0_partition`` holds the application.
The application cannot receive an image that overwrites itself, so **firmware update is performed in MCUboot serial recovery mode**, not in the application.

The application still runs a minimal SMP server (MCUmgr ``os`` group only) so a host can request recovery using an MCUmgr reset with the boot mode parameter while the application is running.
Image upload uses MCUboot on the same UART.

MCUboot is built with :kconfig:option:`SB_CONFIG_MCUBOOT_SIGNATURE_USING_ITS` and **pure Ed25519** signatures.
It does **not** embed the default MCUboot verification key.
At boot, it verifies the image against Ed25519 public keys in ITS at PSA key IDs ``0x40022100`` through ``0x40022103`` (``BL_PUBKEY`` slots), the same model as nRF54H20.

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

.. _nrf9251_smp_single_slot_keys:

Signing key and ITS provisioning
********************************

You need a matching pair:

* **Private** key — used at build time to produce ``zephyr.signed.bin``.
* **Public** key — provisioned on the device in ITS (for example with :file:`all_keys.json`).

The private key used for signing must correspond to the public key in ITS slot ``0x40022100``.
If they do not match, MCUboot refuses to boot the application (signature verification failure).

Generate an Ed25519 key pair (example)
======================================

From the sample directory:

.. code-block:: console

   python3 ${ZEPHYR_BASE}/../bootloader/mcuboot/scripts/imgtool.py keygen \
     -k my_mcuboot_signing.pem -t ed25519

Export the public key if you build :file:`all_keys.json` manually:

.. code-block:: console

   openssl pkey -in my_mcuboot_signing.pem -pubout -out my_mcuboot_public.pem

Build a provisioning JSON file (example)
========================================

Use :ref:`generate_psa_key_attributes_script` with key ID ``0x40022100``, Ed25519 verify, and ``EDDSA_PURE``.
See :ref:`ug_nrf54h20_keys` for the full workflow.

.. code-block:: console

   python3 ${ZEPHYR_BASE}/../nrf/scripts/generate_psa_key_attributes/generate_psa_key_attributes.py \
     --usage VERIFY --id 0x40022100 \
     --type ECC_PUBLIC_KEY_TWISTED_EDWARDS --key-bits 255 \
     --algorithm EDDSA_PURE --location LOCATION_LOCAL_STORAGE \
     --key-from-file my_mcuboot_public.pem --file all_keys.json \
     --persistence PERSISTENCE_DEFAULT

Provision keys on the device
============================

After you know the device serial number (``nrfutil device list``):

.. code-block:: console

   nrfutil device x-provision-keys --serial-number <snr> --key-file all_keys.json

Alternatively, from the |NCS| workspace:

.. code-block:: console

   west ncs-provision upload -k my_mcuboot_signing.pem \
     --keyname BL_PUBKEY --soc nrf9251 --dev-id <snr> --policy revokable

.. important::
   A full chip erase (``west flash --erase``) clears ITS keys on nRF92.
   **Provision again after any erase**, then reset.
   If you see ``ED25519 signature verification failed -136``, that is ``PSA_ERROR_INVALID_HANDLE`` (no key in ITS), not a bad signature.

Optional: auto-provision on flash (development only)
====================================================

Add ``-DSB_EXTRA_CONF_FILE=sysbuild_provision.conf`` to the build command.
The build generates :file:`keyfile.json` from the signing key; ``west flash`` may provision it when that file is present.
Do not use this in production.

.. _nrf9251_smp_single_slot_build:

Building
********

.. |sample path| replace:: :file:`samples/dfu/nrf9251_smp_single_slot`

.. include:: /includes/build_and_run.txt

The sample must be built with sysbuild.

From the sample directory:

.. code-block:: console

   cd nrf/samples/dfu/nrf9251_smp_single_slot

   west build -p -b nrf9251dk/nrf9251/cpuapp --sysbuild . -- \
     -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$PWD/my_mcuboot_signing.pem\"

.. note::
   Kconfig string options must be quoted on the command line as shown.
   Relative paths are resolved from the **west workspace top directory** (``$PWD/...`` or an absolute path avoids mistakes).

   To avoid passing the key on every build, create a local :file:`sysbuild_keys.conf` (do not commit private keys to a shared repo):

   .. code-block:: none

      SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="${APPLICATION_CONFIG_DIR}/my_mcuboot_signing.pem"

   Then:

   .. code-block:: console

      west build -p -b nrf9251dk/nrf9251/cpuapp --sysbuild . -- \
        -DSB_EXTRA_CONF_FILE=sysbuild_keys.conf

Use the **same** signing key option on every rebuild (omit ``-p`` when you only change application source).

Sysbuild produces three images: ``mcuboot``, ``uicr``, and the application.
The SMP upload file is:

.. code-block:: none

   build/nrf9251_smp_single_slot/zephyr/zephyr.signed.bin

.. _nrf9251_smp_single_slot_flash:

Programming the device
**********************

First programming (blank or erased chip)
========================================

#. Build as in :ref:`nrf9251_smp_single_slot_build`.
#. Flash with erase:

   .. code-block:: console

      west flash --erase

#. Provision keys (:ref:`nrf9251_smp_single_slot_keys`).
#. Reset and confirm the application runs on **uart133**.

Later firmware updates (MCUboot + app already on device, keys provisioned)
==========================================================================

* **SMP DFU** — upload ``zephyr.signed.bin`` only; no full reflash required.
* **J-Link reflash of images** — use ``west flash`` **without** ``--erase`` so ITS keys are kept.
  If you must use ``--erase``, provision keys again afterward.

.. _nrf9251_smp_single_slot_test_happy:

End-to-end SMP update (happy path)
**********************************

#. Build and flash as in :ref:`nrf9251_smp_single_slot_build` and :ref:`nrf9251_smp_single_slot_flash`.
#. Open a serial terminal on **uart133** at **115200 8N1**.
#. Reset and confirm::

      nRF9251 SMP single-slot sample, firmware version 1

   LED0 blinks at a rate that matches ``APP_FW_VERSION`` in :file:`src/main.c`.
#. Increase ``APP_FW_VERSION`` in :file:`src/main.c` and rebuild (same signing key as above):

   .. code-block:: console

      west build -b nrf9251dk/nrf9251/cpuapp --sysbuild . -- \
        -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$PWD/my_mcuboot_signing.pem\"

#. Set ``PORT`` to **uart132** (not the DK console unless it is wired to ``uart132``):

   .. code-block:: console

      export PORT=/dev/ttyUSB0

#. Enter MCUboot serial recovery:

   * Hold **Button 0** (``P0.01``), press **RESET**, release the button, or
   * Within **5 seconds** after reset, send any MCUmgr frame on **uart132**, or
   * From the running application, MCUmgr ``os`` reset with ``boot_mode`` (see :file:`dts/boot_mode.dtsi` and :ref:`mcuboot_serial_recovery`).

   On **uart133**, MCUboot should stay in serial recovery instead of booting the application.
#. Upload on **uart132** using `mcumgr`_:

   .. code-block:: console

      mcumgr --conntype serial --connstring "dev=$PORT,baud=115200" \
        image upload build/nrf9251_smp_single_slot/zephyr/zephyr.signed.bin
      mcumgr --conntype serial --connstring "dev=$PORT,baud=115200" reset

   Or ``nrfutil mcu-manager serial``:

   .. code-block:: console

      nrfutil mcu-manager serial image-upload --serial-port $PORT --image-number 0 \
        --firmware build/nrf9251_smp_single_slot/zephyr/zephyr.signed.bin
      nrfutil mcu-manager serial reset --serial-port $PORT

   There is no ``image confirm`` step in single-slot mode.
#. On **uart133**, confirm the new firmware version and LED behavior.

.. note::
   SMP upload may report success when the transfer completes.
   Signature checking happens when MCUboot **boots** the slot, not necessarily during upload.

.. _nrf9251_smp_single_slot_test_reject:

Verifying that MCUboot rejects bad images
*****************************************

Use these tests on **uart133** after reset.
A passing test means the **application does not run** (no new version line) and MCUboot reports failure or stays in serial recovery.

Corrupted signature (tampered image)
====================================

Pure Ed25519 covers the image header and payload.
Corrupting a byte that is already ``0x00`` (common in the 2 KiB MCUboot header) does **not** change the file.

#. Build a good ``zephyr.signed.bin`` with ``my_mcuboot_signing.pem``.
#. On the host:

   .. code-block:: console

      BIN=build/nrf9251_smp_single_slot/zephyr/zephyr.signed.bin
      KEY=$PWD/my_mcuboot_signing.pem
      cp "$BIN" /tmp/bad.signed.bin
      printf '\x55' | dd of=/tmp/bad.signed.bin bs=1 seek=3000 conv=notrunc 2>/dev/null
      python3 ${ZEPHYR_BASE}/../bootloader/mcuboot/scripts/imgtool.py verify -k "$KEY" /tmp/bad.signed.bin

   ``imgtool verify`` must **fail** before you try the device.
#. Enter serial recovery and upload ``/tmp/bad.signed.bin``, then reset.
#. **Expect:** no new application version; MCUboot logs signature or boot failure and does not chain-load the app.
#. Recover by uploading a good ``zephyr.signed.bin`` signed with ``my_mcuboot_signing.pem``.

Wrong signing key
=================

The device still has only the **provisioned** public key; the image is signed with a different private key.

#. Generate a second key (do **not** provision it):

   .. code-block:: console

      python3 ${ZEPHYR_BASE}/../bootloader/mcuboot/scripts/imgtool.py keygen \
        -k wrong_signing.pem -t ed25519

#. Bump ``APP_FW_VERSION`` to a distinctive value (for example ``99``) and build:

   .. code-block:: console

      west build -b nrf9251dk/nrf9251/cpuapp --sysbuild . -- \
        -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$PWD/wrong_signing.pem\"

   Optional host check (well-formed image, wrong key for ITS):

   .. code-block:: console

      python3 ${ZEPHYR_BASE}/../bootloader/mcuboot/scripts/imgtool.py verify -k wrong_signing.pem \
        build/nrf9251_smp_single_slot/zephyr/zephyr.signed.bin

#. Enter serial recovery, upload that ``zephyr.signed.bin``, reset.
#. **Expect:** version ``99`` does **not** appear; verification fails on boot.
#. Rebuild with ``my_mcuboot_signing.pem``, upload the good image, and confirm the app runs again.

Troubleshooting
***************

.. list-table::
   :header-rows: 1

   * - Symptom
     - Likely cause
   * - ``signature verification failed -136`` after flash
     - ITS keys erased by ``--erase``; provision again
   * - ``signature verification failed`` (other codes) after SMP
     - Image signed with a key that does not match ITS; or corrupted image
   * - Upload has no effect
     - Wrong serial port — SMP must be on **uart132**, not **uart133**
   * - Application boots immediately
     - Serial recovery not entered — retry button + reset or the 5 s MCUmgr window
   * - Upload succeeds but version unchanged
     - Wrong ``zephyr.signed.bin``, recovery not active, or tamper test was a no-op (``0x00`` over ``0x00``)
   * - Build error ``malformed string literal`` for key file
     - Quote the path: ``-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$PWD/my_mcuboot_signing.pem\"``
   * - ``west sign can't find file`` under ``$WEST_TOPDIR/...``
     - Use ``$PWD/...`` or ``\${APPLICATION_CONFIG_DIR}/...`` in :file:`sysbuild_keys.conf`
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
