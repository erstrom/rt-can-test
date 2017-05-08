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

#include "can.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>

struct can_hdl {
	int fd;
	int ifindex;
};

static int can_socket_cfg(struct can_hdl *hdl, struct can_cfg *cfg)
{
	int mtu, enable_canfd = 1, ret = 0;
	struct sockaddr_can addr;
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, cfg->ifname);

	hdl->fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (hdl->fd < 0) {
		fprintf(stderr, "%s Error while opening socket. errno: %d\n",
			__func__, errno);
		ret = -1;
		goto out;
	}

	if ((ioctl(hdl->fd, SIOCGIFINDEX, &ifr)) == -1) {
		fprintf(stderr, "%s Error getting interface index. errno: %d\n",
			__func__, errno);
		ret = -2;
		goto out;
	}

	hdl->ifindex = ifr.ifr_ifindex;

	if (cfg->mtu > CAN_MTU) {
		/* check if the frame fits into the CAN netdevice */
		if (ioctl(hdl->fd, SIOCGIFMTU, &ifr) == -1) {
			fprintf(stderr,
				"%s Error getting interface MTU. errno: %d\n",
				__func__, errno);
			ret = -3;
			goto out;
		}

		mtu = ifr.ifr_mtu;
		if (mtu != CANFD_MTU) {
			fprintf(stderr,
				"%s Error: Interface MTU (%d) is not valid. Expected %zu\n",
				__func__, mtu, CANFD_MTU);
			ret = -4;
			goto out;
		}

		/* Try to switch the socket into CAN FD mode */
		if (setsockopt(hdl->fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
			       &enable_canfd, sizeof(enable_canfd))) {
			fprintf(stderr,
				"%s Error: Unable to enable CAN FD support. errno: %d\n",
				__func__, errno);
			ret = -5;
			goto out;
		}
	}

	addr.can_family  = AF_CAN;
	addr.can_ifindex = hdl->ifindex;
	ret = bind(hdl->fd, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		fprintf(stderr, "%s Error in socket bind. errno: %d\n",
			__func__, errno);
		ret = -6;
		goto out;
	}

	if (cfg->rx_filter)
		/* Setup CAN ID filter*/
		setsockopt(hdl->fd, SOL_CAN_RAW, CAN_RAW_FILTER,
			   cfg->rx_filter, cfg->rx_filter_len);

out:
	return ret;
}

int can_open(struct can_hdl **hdl, struct can_cfg *cfg)
{
	int ret;

	*hdl = malloc(sizeof(struct can_hdl));
	if (!*hdl) {
		ret = -1;
		goto err;
	}

	memset(*hdl, 0, sizeof(struct can_hdl));

	ret = can_socket_cfg(*hdl, cfg);
	if (ret != 0)
		goto err;

	goto out;
err:
	free(*hdl);
	*hdl = NULL;
out:
	return ret;
}

int can_close(struct can_hdl **hdl)
{
	int ret;

	if (!hdl || !(*hdl)) {
		ret = -1;
		goto out;
	}

	ret = close((*hdl)->fd);
	if (ret < 0)
		ret = -errno;

	free(*hdl);
	*hdl = NULL;
out:
	return ret;
}

ssize_t can_read(struct can_hdl *hdl, struct canfd_frame *frame)
{
	return read(hdl->fd, frame, sizeof(struct canfd_frame));
}

ssize_t can_write(struct can_hdl *hdl, const struct canfd_frame *frame)
{
	ssize_t nbytes = -1;
	struct sockaddr_can addr;

	addr.can_ifindex = hdl->ifindex;
	addr.can_family  = AF_CAN;

	nbytes = sendto(hdl->fd, frame, sizeof(struct canfd_frame),
			MSG_DONTWAIT, (struct sockaddr *)&addr, sizeof(addr));

	if ((nbytes < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
		/* Socket TX buffer is full. This could happen if the unit is
		 * not connected to a CAN bus.
		 */
		nbytes = 0;

	return nbytes;
}
