/*
 * Copyright (c) 2017 Erik Stromdahl <erik.stromdahl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _CAN_H_
#define _CAN_H_

#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/types.h>

struct can_hdl;

struct can_cfg {
	/**
	 * CAN interface name
	 */
	char *ifname;
	/**
	 * Requested CAN MTU (Maximum Transmission Unit).
	 * If the value is greater than 8, the interface will be put
	 * in FD (Flexible Datarate) mode.
	 */
	size_t mtu;
	/**
	 * Optional RX filter array.
	 * If NULL, no filtering will be used.
	 */
	struct can_filter *rx_filter;
	/**
	 * Length of the rx_filter array
	 */
	size_t rx_filter_len;
};

int can_open(struct can_hdl **hdl, struct can_cfg *cfg);

int can_close(struct can_hdl **hdl);

ssize_t can_read(struct can_hdl *hdl, struct canfd_frame *frame);

ssize_t can_write(struct can_hdl *hdl, const struct canfd_frame *frame);

#endif /*_CAN_H_*/

