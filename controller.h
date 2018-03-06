#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "openflow.h"
#include <stdint.h>
#include <netinet/ether.h>
#include <netinet/in.h>


enum rw_status{
	READ,
	WRITE,
	DISCONNECTED
};


/*****************************************
 *
 * Save and adjacency matrix of the connected network
 *
 *****************************************/
struct network{
	struct node **devices;
	uint8_t num_conn_devices;
	uint8_t max_network_size;
};

struct node{
	struct ether_addr hw_addr;
	struct in_addr    ip_addr; 
	struct node *next;
	uint8_t is_switch;
	uint8_t device_num; //used to index into the adj matrix
	uint8_t port_num;
};


struct of_controller{
	uint64_t num_connected_switches; /* hold number of connected switches */
	uint32_t max_connected_switches; /* upper limit of all connected switches */
	struct of_switch *switch_list;   /* list of all connected switches */
};


//chandler wrote all of this part
struct of_switch{
	enum rw_status  	rw;
	enum ofp_type 		of_status;
	uint8_t			reading_header;
	uint32_t 		xid;
	uint16_t 		socket_fd;
	uint16_t 		bytes_read;
	uint16_t 		bytes_written;
	uint16_t 		bytes_expected;
	char 			*read_buffer;
	char 			*write_buffer;
	uint16_t 		write_buffer_size;
	uint16_t 		read_buffer_size;
	uint8_t 		config_set; /* boolean if set or not */
	uint8_t 		ports_requested; /* if ports requested already */
	uint8_t                 features_requested;
	uint8_t 		default_flow_set;
	uint8_t 		timeout; //in seconds
	struct ofp_port 	connected_ports[50]; //right now assume 50 max ports
	struct ether_addr	connected_hosts[50]; //one hw addr for each port
};


#endif
