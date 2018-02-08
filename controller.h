#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>


enum status{
	ALIVE,
	READING,
	PROCESSING,
	WRITING,
	DEAD	
};

struct of_controller{
	uint64_t num_connected_switches; /* hold number of connected switches */
	uint32_t max_connected_switches; /* upper limit of all connected switches */
	struct of_switch *switch_list; /* list of all connected switches */
};

struct of_switch{
	enum status switch_status;
	uint32_t socket_fd;
	uint16_t bytes_read;
	uint16_t bytes_written;
	char *read_buffer;
	char *write_buffer;
	uint16_t write_buffer_size;
	uint16_t read_buffer_size;
		
};


#endif
