/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_C_COMMON_H
#define SCREENCAPTURE_C_COMMON_H

#include <stdint.h>

#ifndef SCW_EXPORT
#define SCW_EXPORT __attribute__((visibility("default")))
#endif


typedef struct
{
	void* connection;
	int pipeWireFd;
	uint32_t pipeWireNode;
} SharedScreen_t;




#endif //SCREENCAPTURE_C_COMMON_H