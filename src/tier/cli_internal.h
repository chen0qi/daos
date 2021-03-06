/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * DCT Internal use functions
 */


#ifndef __DCT_INTERNAL_H__
#define __DCT_INTERNAL_H__
#include <daos_tier.h>

struct dc_tier_context {
	daos_tier_info_t *dtc_this;
	daos_tier_info_t *dtc_colder;
};


extern struct dc_tier_context g_tierctx;

daos_tier_info_t *
tier_setup_cold_tier(const uuid_t uuid, const char *grp);

daos_tier_info_t *
tier_setup_this_tier(const uuid_t uuid, const char *grp);

daos_tier_info_t *
tier_lookup(const char *tier_id);

crt_group_t *
tier_crt_group_lookup(const char *tier_id);

void
tier_teardown(void);

#endif /* __DCT_INTERNAL_H__ */
