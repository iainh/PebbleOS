# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_clar_test(test_qemu_serial
  SOURCES
    src/fw/drivers/qemu/qemu_serial_util.c
    src/fw/util/shared_circular_buffer.c
    tests/fakes/fake_rtc.c
  OVERRIDES dummy_board
)

pbl_clar_test(test_flash_api
  SOURCES
    src/fw/drivers/flash/flash_api.c
  OVERRIDES dummy_board
)

pbl_clar_test(test_flash_erase
  SOURCES
    src/fw/drivers/flash/flash_erase.c
  PLATFORMS obelix
  OVERRIDES dummy_board
)

pbl_clar_test(test_nrf5_pdm
  SOURCES
    src/fw/drivers/mic/nrf5/pdm.c
  PLATFORMS asterix
  DEFINES
    CONFIG_SOC_NRF52
    NRF52840_XXAA
    NRFX_PDM_ENABLED=1
    NRFX_PWM_ENABLED=1
    NRFX_PWM0_ENABLED=1
    NRFX_SPIM_ENABLED=1
    NRFX_SPIM3_ENABLED=1
    NRFX_TIMER_ENABLED=1
    NRFX_TIMER1_ENABLED=1
  INCLUDES
    third_party/cmsis_core/CMSIS/CMSIS/Core/Include
    third_party/hal_nordic
    third_party/hal_nordic/nrfx
    third_party/hal_nordic/nrfx/hal
    third_party/hal_nordic/nrfx/mdk
    third_party/hal_nordic/nrfx/drivers/include
    third_party/hal_nordic/nrfx/templates
)
