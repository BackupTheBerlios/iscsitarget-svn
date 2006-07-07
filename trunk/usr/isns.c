/*
 * iSNS functions
 *
 * Copyright (C) 2006 FUJITA Tomonori <tomof@acm.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "iscsid.h"
#include "isns_proto.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BUFSIZE (1 << 18)

struct isns_io {
	char *buf;
	int offset;
};

static int use_isns, isns_fd;
static struct isns_io isns_rx, isns_tx;
static uint32_t transaction, current_timeout = 60; /* seconds */
static char eid[ISCSI_NAME_LEN];
static uint8_t ip[16]; /* IET supoprts only one portal */
static struct sockaddr_storage ss;

static int isns_get_ip(int fd)
{
	int err, i;
	uint32_t addr;
	struct sockaddr_storage lss;
	socklen_t slen = sizeof(lss);

	err = getsockname(fd, (struct sockaddr *) &lss, &slen);
	if (err) {
		log_error("getsockname error %s!", gai_strerror(err));
		return err;
	}

	err = getnameinfo((struct sockaddr *) &lss, sizeof(lss),
			  eid, sizeof(eid), NULL, 0, 0);
	if (err) {
		log_error("getaddrinfo error %s!", gai_strerror(err));
		return err;
	}

	switch (lss.ss_family) {
	case AF_INET:
		addr = (((struct sockaddr_in *) &lss)->sin_addr.s_addr);

		ip[10] = ip[11] = 0xff;
		ip[15] = 0xff & (addr >> 24);
		ip[14] = 0xff & (addr >> 16);
		ip[13] = 0xff & (addr >> 8);
		ip[12] = 0xff & addr;
		break;
	case AF_INET6:
		for (i = 0; i < ARRAY_SIZE(ip); i++)
			ip[i] = ((struct sockaddr_in6 *) &lss)->sin6_addr.s6_addr[i];
		break;
	}

	return 0;
}

static int isns_connect(void)
{
	int fd, err;

	fd = socket(ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		log_error("unable to create (%s) %d!", strerror(errno),
			  ss.ss_family);
		return -1;
	}

	err = connect(fd, (struct sockaddr *) &ss, sizeof(ss));
	if (err < 0) {
		log_error("unable to connect (%s) %d!", strerror(errno),
			  ss.ss_family);
		close(fd);
		return -1;
	}
	log_error("connect (%s) %d!", strerror(errno), ss.ss_family);

	if (!strlen(eid)) {
		err = isns_get_ip(fd);
		if (err) {
			close(fd);
			return -1;
		}
	}

	isns_fd = fd;
	isns_set_fd(fd);

	return fd;
}

static void isns_hdr_init(struct isns_hdr *hdr, uint16_t function,
			  uint16_t length, uint16_t flags,
			  uint16_t trans, uint16_t sequence)
{
	hdr->version = htons(0x0001);
	hdr->function = htons(function);
	hdr->length = htons(length);
	hdr->flags = htons(flags);
	hdr->transaction = htons(trans);
	hdr->sequence = htons(sequence);
}

static int isns_tlv_set(struct isns_tlv **tlv, uint32_t tag, uint32_t length,
			void *value)
{
	if (length)
		memcpy((*tlv)->value, value, length);
	if (length % ISNS_ALIGN)
		length += (ISNS_ALIGN - (length % ISNS_ALIGN));

	(*tlv)->tag = htonl(tag);
	(*tlv)->length = htonl(length);

	length += sizeof(struct isns_tlv);
	*tlv = (struct isns_tlv *) ((char *) *tlv + length);

	return length;
}

static int isns_attr_query(void)
{
	uint16_t flags, length = 0;
	char buf[4096];
	struct isns_hdr *hdr = (struct isns_hdr *) buf;
	struct isns_tlv *tlv;
	struct target *target;

	if (list_empty(&targets_list))
		return 0;

	if (!isns_fd)
		if (isns_connect() < 0)
			return 0;

	memset(buf, 0, sizeof(buf));
	tlv = (struct isns_tlv *) hdr->pdu;

	target = list_entry(targets_list.q_forw, struct target, tlist);

	length += isns_tlv_set(&tlv, ISNS_ATTR_ISCSI_NAME,
			       strlen(target->name), target->name);
	length += isns_tlv_set(&tlv, 0, 0, 0);
	length += isns_tlv_set(&tlv, ISNS_ATTR_PORTAL_IP_ADDRESS, 0, 0);

	flags = ISNS_FLAG_CLIENT | ISNS_FLAG_LAST_PDU | ISNS_FLAG_FIRST_PDU;
	isns_hdr_init(hdr, ISNS_FUNC_DEV_ATTR_QRY, length, flags,
		      ++transaction, 0);

	write(isns_fd, buf, length + sizeof(struct isns_hdr));

	return 0;
}

static int isns_deregister(void)
{
	uint16_t flags, length = 0;
	char buf[4096];
	struct isns_hdr *hdr = (struct isns_hdr *) buf;
	struct isns_tlv *tlv;
	struct target *target;

	if (list_empty(&targets_list))
		return 0;

	if (!isns_fd)
		if (isns_connect() < 0)
			return 0;

	memset(buf, 0, sizeof(buf));
	tlv = (struct isns_tlv *) hdr->pdu;

	target = list_entry(targets_list.q_forw, struct target, tlist);

	length += isns_tlv_set(&tlv, ISNS_ATTR_ISCSI_NAME,
			       strlen(target->name), target->name);
	length += isns_tlv_set(&tlv, 0, 0, 0);
	length += isns_tlv_set(&tlv, ISNS_ATTR_ENTITY_IDENTIFIER,
			       strlen(eid), eid);

	flags = ISNS_FLAG_CLIENT | ISNS_FLAG_LAST_PDU | ISNS_FLAG_FIRST_PDU;
	isns_hdr_init(hdr, ISNS_FUNC_DEV_DEREG, length, flags,
		      ++transaction, 0);

	write(isns_fd, buf, length + sizeof(struct isns_hdr));
	return 0;
}

int isns_target_register(char *name)
{
	char buf[4096];
	uint16_t flags, length = 0;
	struct isns_hdr *hdr = (struct isns_hdr *) buf;
	struct isns_tlv *tlv;
	uint32_t port = htonl(ISCSI_LISTEN_PORT);
	uint32_t node = htonl(ISNS_NODE_TARGET);
	uint32_t type = htonl(2);
	int first = list_empty(&targets_list);
	struct target *target;

	if (!use_isns)
		return 0;

	if (!isns_fd)
		if (isns_connect() < 0)
			return 0;

	memset(buf, 0, sizeof(buf));
	tlv = (struct isns_tlv *) hdr->pdu;

	if (first)
	        length += isns_tlv_set(&tlv, ISNS_ATTR_ISCSI_NAME,
				       strlen(name), name);
        else {
	        target = list_entry(targets_list.q_back, struct target, tlist);
	        length += isns_tlv_set(&tlv, ISNS_ATTR_ISCSI_NAME,
				       strlen(target->name), target->name);
	}
	length += isns_tlv_set(&tlv, ISNS_ATTR_ENTITY_IDENTIFIER,
			       strlen(eid), eid);

	length += isns_tlv_set(&tlv, 0, 0, 0);
	length += isns_tlv_set(&tlv, ISNS_ATTR_ENTITY_IDENTIFIER,
			       strlen(eid), eid);
	if (first) {
		length += isns_tlv_set(&tlv, ISNS_ATTR_ENTITY_PROTOCOL,
				       sizeof(type), &type);
		length += isns_tlv_set(&tlv, ISNS_ATTR_PORTAL_IP_ADDRESS,
				       sizeof(ip), &ip);
		length += isns_tlv_set(&tlv, ISNS_ATTR_PORTAL_PORT,
				       sizeof(port), &port);
	}
	length += isns_tlv_set(&tlv, ISNS_ATTR_ISCSI_NAME, strlen(name), name);
	length += isns_tlv_set(&tlv, ISNS_ATTR_ISCSI_NODE_TYPE,
			       sizeof(node), &node);

	flags = ISNS_FLAG_CLIENT | ISNS_FLAG_LAST_PDU | ISNS_FLAG_FIRST_PDU;
	isns_hdr_init(hdr, ISNS_FUNC_DEV_ATTR_REG, length, flags,
		      ++transaction, 0);

	write(isns_fd, buf, length + sizeof(struct isns_hdr));

	return 0;
}

int isns_target_deregister(char *name)
{
	char buf[4096];
	uint16_t flags, length = 0;
	struct isns_hdr *hdr = (struct isns_hdr *) buf;
	struct isns_tlv *tlv;
	int last = list_empty(&targets_list);

	if (!use_isns)
		return 0;

	if (!isns_fd)
		if (isns_connect() < 0)
			return 0;

	memset(buf, 0, sizeof(buf));
	tlv = (struct isns_tlv *) hdr->pdu;

	length += isns_tlv_set(&tlv, ISNS_ATTR_ISCSI_NAME, strlen(name), name);
	length += isns_tlv_set(&tlv, 0, 0, 0);
	if (last)
		length += isns_tlv_set(&tlv, ISNS_ATTR_ENTITY_IDENTIFIER,
				       strlen(eid), eid);
	else
		length += isns_tlv_set(&tlv, ISNS_ATTR_ISCSI_NAME,
				       strlen(name), name);

	flags = ISNS_FLAG_CLIENT | ISNS_FLAG_LAST_PDU | ISNS_FLAG_FIRST_PDU;
	isns_hdr_init(hdr, ISNS_FUNC_DEV_DEREG, length, flags,
		      ++transaction, 0);

	write(isns_fd, buf, length + sizeof(struct isns_hdr));

	return 0;
}

int isns_handle(int is_timeout, int *timeout)
{
	int err;
	struct isns_io *rx = &isns_rx;
	struct isns_hdr *hdr = (struct isns_hdr *) rx->buf;
	uint16_t function, length, flags, transaction, sequence;
	uint32_t result;

	if (is_timeout)
		return isns_attr_query();

	if (rx->offset < sizeof(*hdr)) {
		err = read(isns_fd, rx->buf + rx->offset,
			   sizeof(*hdr) - rx->offset);
		if (err < 0) {
			log_error("read header error %d %d %d %d!",
				  isns_fd, err, errno, rx->offset);
			return -1;
		} else if (err == 0) {
			log_error("close error %d %d %s",
				  __LINE__, isns_fd, strerror(errno));
			close(isns_fd);
			isns_fd = 0;
			isns_set_fd(0);
			return -1;
		}

		log_debug(1, "got %d %d bytes!", isns_fd, err);
		rx->offset += err;

		if (rx->offset < sizeof(*hdr)) {
			log_error("header wait %d %d", rx->offset, err);
			return 0;
		}
	}

	/* Now we got a complete header */
	function = ntohs(hdr->function);
	length = ntohs(hdr->length);
	flags = ntohs(hdr->flags);
	transaction = ntohs(hdr->transaction);
	sequence = ntohs(hdr->sequence);

	log_error("got a header %x %u %x %u %u", function, length, flags,
		  transaction, sequence);

	if (length + sizeof(*hdr) > BUFSIZE) {
		log_error("FIXME we cannot handle this yet %u!", length);
		return -1;
	}

	if (rx->offset < length + sizeof(*hdr)) {
		err = read(isns_fd, rx->buf + rx->offset,
			   length + sizeof(*hdr) - rx->offset);
		log_debug(1, "got a header %x %u %x %u %u",
			  function, length, flags, transaction, sequence);
		if (err < 0) {
			log_error("read pdu error %d %d %d!",
				  isns_fd, err, errno);
			return -1;
		} else if (err == 0) {
			log_error("close error %d %d %s",
				  __LINE__, isns_fd, strerror(errno));
			close(isns_fd);
			isns_fd = 0;
			isns_set_fd(0);
			return -1;
		}

		rx->offset += err;

		if (rx->offset < length + sizeof(*hdr)) {
			log_error("data wait %d %d", rx->offset, err);
			return 0;
		}
	}

	/* Now we got everything. */
	rx->offset = 0;
	result = ntohl((uint32_t) hdr->pdu[0]);

	switch (function) {
	case ISNS_FUNC_DEV_ATTR_REG_RSP:
	case ISNS_FUNC_DEV_ATTR_QRY_RSP:
	case ISNS_FUNC_DEV_DEREG_RSP:
		break;
	default:
		log_error("function error %x %u %x %u %u", function, length,
			  flags, transaction, sequence);
	}

	return 0;
}

int isns_init(char *addr)
{
	int err;
	char *buf, port[8];
	struct addrinfo hints, *res;

	snprintf(port, sizeof(port), "%d", ISNS_PORT);
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	err = getaddrinfo(addr, (char *) &port, &hints, &res);
	if (err) {
		log_error("getaddrinfo error %s!", gai_strerror(err));
		return -1;
	}
	memcpy(&ss, res->ai_addr, sizeof(ss));
	freeaddrinfo(res);

	buf = calloc(2, BUFSIZE);
	if (!buf) {
		log_error("oom!");
		return -1;
	}

	isns_rx.buf = buf;
	isns_rx.offset = 0;
	isns_tx.buf = buf + BUFSIZE;
	isns_tx.offset = 0;

	use_isns = 1;

	return current_timeout * 1000;
}

void isns_exit(void)
{
	isns_deregister();
	free(isns_rx.buf);

	close(isns_fd);
}
