#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "openflow.h"
#include <stdint.h>

enum rw_status{
	READ,
	WRITE,
	DISCONNECTED
};

struct of_controller{
	uint64_t num_connected_switches; /* hold number of connected switches */
	uint32_t max_connected_switches; /* upper limit of all connected switches */
	struct of_switch *switch_list;   /* list of all connected switches */
};

struct of_switch{
	enum rw_status  	rw;
	enum ofp_type 		of_status;
	uint32_t 		xid;
	uint32_t 		socket_fd;
	uint16_t 		bytes_read;
	uint16_t 		bytes_written;
	uint16_t 		bytes_expected;
	char 			*read_buffer;
	char 			*write_buffer;
	uint16_t 		write_buffer_size;
	uint16_t 		read_buffer_size;
};


#endif
