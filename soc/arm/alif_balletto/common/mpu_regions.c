/*
 * Copyright (c) 2024 Alif Semiconductor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/slist.h>
#include <zephyr/arch/arm/mpu/arm_mpu.h>

#define ALIF_HOST_PERIPHERAL_BASE	0x1A000000
#define ALIF_HOST_PERIPHERAL_SIZE	0x1000000

static const struct arm_mpu_region mpu_regions[] = {
	/* Region 0 */
	MPU_REGION_ENTRY("FLASH_0", CONFIG_FLASH_BASE_ADDRESS,
			 REGION_FLASH_ATTR(CONFIG_FLASH_BASE_ADDRESS, CONFIG_FLASH_SIZE * 1024)),
	/* Region 1 */
	MPU_REGION_ENTRY("SRAM_0", CONFIG_SRAM_BASE_ADDRESS,
			 REGION_RAM_ATTR(CONFIG_SRAM_BASE_ADDRESS, CONFIG_SRAM_SIZE * 1024)),
	/* Region 3 */
	MPU_REGION_ENTRY("PERIPHERALS", ALIF_HOST_PERIPHERAL_BASE,
			 REGION_DEVICE_ATTR(ALIF_HOST_PERIPHERAL_BASE,
							ALIF_HOST_PERIPHERAL_SIZE)),
};

const struct arm_mpu_config mpu_config = {
	.num_regions = ARRAY_SIZE(mpu_regions),
	.mpu_regions = mpu_regions,
};
