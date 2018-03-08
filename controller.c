/* Controller.c
 * Author: Calvin Laverty
 * Last Edit: 2/8/18
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include "openflow.h"
#include "rw_packets.h"
#include "controller.h"
#include "smartalloc.h"


/* GLOBAL DEFS */
struct addrinfo hints, *matches, *server;
int get_info_ret;

struct network *network_graph;

struct of_controller idk_man; 		/* struct to hold all controller info */

char port[15] = "6653"; 		/* common port for Openflow */
char controller_addr[15] = "127.0.0.1"; /* usually run locally */

int listening_socket_fd = 0; 		/* holds the fd for listening socket */
int largest_fd = 1; 			/* held for select loop */

int interrupt = 0; 			/* for SIGINT */

int xid_generator = 1;

fd_set read_sockets;
fd_set write_sockets;
fd_set error_sockets;
/* END GLOBAL DEFS */


/* print out the network connections */
void print_network(struct network *graph){
	fprintf(stderr, "^^^^^^^^^^^^^^^^^^^^^^^^^\n"
			"^^^^ NETWORK GRAPH ^^^^^^\n"
			"^^^^^^^^^^^^^^^^^^^^^^^^^\n\n");
	
	int i = 0;
	for(; i < graph->max_network_size; i++){
		if(graph->devices[i] != NULL){
			if(graph->devices[i]->is_switch){
				fprintf(stderr, "Switch %d: ", graph->devices[i]->device_num);
			}
			else{
				fprintf(stderr, "Device %d: ", graph->devices[i]->device_num);
			}
			struct node *connection = graph->devices[i]->next;
			while(connection != NULL){
				fprintf(stderr, "%d, ", connection->device_num);
				connection = connection->next;
			}
			fprintf(stderr, "END\n");
		}	
	}	
	fprintf(stderr, "\n^^^^^^^^^^^^^^^^^^^^^^^^^\n");
}


int recursive_loop_exists(int find_node, int last_node){
	int ret = 0;
	int i = 0;
	while((network_graph->devices[i] != NULL) && network_graph->devices[i]->device_num != find_node) i++;
	//found node from last call, check if already discovered
	//fprintf(stderr, "Recursive call: Last: %d Finding: %d\n", last_node, find_node);
	if(network_graph->devices[i]->discovered == 0){
		//fprintf(stderr, "Marking %d as discovered (recusive), parent: %d\n", network_graph->devices[i]->device_num, last_node);
		network_graph->devices[i]->discovered = 1;
		struct node *next_node = network_graph->devices[i]->next;
		while((next_node != NULL) && (ret == 0)){
			if(next_node->device_num != last_node){
				int j = 0;
				while((network_graph->devices[j] != NULL) && (network_graph->devices[j]->device_num != next_node->device_num)) j++;
				if(network_graph->devices[j]->discovered){
					//we found a connection that is already discovered and was not directly connected
					//fprintf(stderr, "FOUND A CYCLE (%d was already discovered)!\n", network_graph->devices[j]->device_num);
					//ret = network_graph->devices[i]->device_num;
					ret = network_graph->devices[j]->device_num;
					return ret;
				}
				ret = recursive_loop_exists(next_node->device_num, find_node);
			}
			next_node = next_node->next;
		}
	}
	return ret;
}


/* performs DFS on the network graph in order
 * to find if there are loops
 */
int loop_exists(void){
	int loop = 0;
	int i = 0;
	for(; i < network_graph->max_network_size; i++){
		if((network_graph->devices[i] != NULL) && (network_graph->devices[i]->discovered == 0)){
			//fprintf(stderr, "Marking %d as discovered (outer)\n", network_graph->devices[i]->device_num);
			network_graph->devices[i]->discovered = 1;
			struct node *next_node = network_graph->devices[i]->next;
			while(next_node != NULL){
				loop = recursive_loop_exists(next_node->device_num, network_graph->devices[i]->device_num);
				if(loop != 0){
					break;
				}
				next_node = next_node->next;

			}			
		}
	}	


	//restore all of the nodes back to being undiscovered for next run
	i = 0;
	for(; i < network_graph->max_network_size; i++){
		if(network_graph->devices[i] != NULL){
			network_graph->devices[i]->discovered = 0;
		}
	}	
	return loop;
}

void add_connection(uint8_t dest, uint8_t src, uint32_t dst_port, uint32_t src_port){
	int i = 0;
	for(; i < network_graph->max_network_size; i++){
		if(network_graph->devices[i] != NULL){
			struct node *new;
			if((network_graph->devices[i]->device_num == dest) || (network_graph->devices[i]->device_num == src)){
				new = network_graph->devices[i];	
				int match = new->device_num;
				int duplicate = 0;
				while(new->next != NULL){
					new = new->next;
					//TO DO: we should check along the way if we already have a connection here
					if(new->device_num == dest || new->device_num == src){
						//fprintf(stderr, "DUPLICATE? Match: %d, Device num: %d\n", match, new->device_num);
						duplicate = 1;
						break;
					}
				}
				if(duplicate){
					continue;
				}
				new->next = calloc(sizeof(struct node), 1);
				new = new->next; //move to the newly created node
				if(match == dest){
					//save the src
					new->device_num = src;
					new->port_num   = src_port;
				}
				else{
					//save the dest
					new->device_num = dest;
					new->port_num   = dst_port; //always 0
				}
				new->next = NULL; //set the next item to be null
			}
		}	
		
	}

	//TO DO: After each connection, check to see if we have a loop
	int looped_device = 0;
	//print_network(network_graph);
	if((looped_device = loop_exists())){
		fprintf(stderr, "Theres a loop!\n");
		print_network(network_graph);
		int i = 0;
		while(idk_man.switch_list[i].socket_fd != looped_device) i++;
		change_port_behavior(&idk_man.switch_list[i], src_port, 0);
	}
}

/* Free all buffers for connected and used switches */
void free_switch_buffers(struct of_switch *switches){
	int i = 0;
	for(; i < idk_man.num_connected_switches; i++){
		if(switches->rw != DISCONNECTED){
			if(switches->read_buffer != NULL){
				free(switches->read_buffer);
			}
			if(switches->write_buffer != NULL){
				free(switches->write_buffer);
			}
		}
	}	
}

/* free all allocated memory held by the controller */
void free_controller_mem(){
	free_switch_buffers(idk_man.switch_list);
	if(idk_man.switch_list != NULL){
		free(idk_man.switch_list);
	}
}

void free_network_graph(struct network *graph){
	int i = 0;
	for(; i < graph->max_network_size; i++){
		if(graph->devices[i] != NULL){
			struct node *curr = graph->devices[i];
			struct node *next_node = NULL;
			while(curr != NULL){
				next_node = curr->next;
				free(curr);
				curr = next_node;	
			}
		}
	}
	free(graph->devices);
	free(graph);
}

/* when SIGINT is received, this is called to clean up */
void controller_exit(void){
	interrupt = 1; 
	fprintf(stderr, "\n=====================================\n"
			"\n  OPENFLOW CONTROLLER SHUTTING DOWN  \n"
			"\n=====================================\n");
}

/* Called when there are more switches that need to be connected to the controller 
 * It is highly unlikely we will have more than 100 switches connected to the controller
 */
void resize_switch_list(){
	idk_man.max_connected_switches *= 2;
	idk_man.switch_list = realloc(idk_man.switch_list, sizeof(struct of_switch) * idk_man.max_connected_switches);
	if(idk_man.switch_list == NULL){
		fprintf(stderr, "Unable to hold info for %d switches... \n", idk_man.max_connected_switches);
		controller_exit();
	}
}

/* sets the socket options on each connection to be non-blocking 
 * NOTE: we still have to check if E_WOULD_BLOCK after writing
 */
void make_non_blocking(int fd){
	int current_options = 0;

	current_options = fcntl(fd, F_GETFL); //get fd flags
	if(current_options == -1){
		fprintf(stderr, "Error getting options for fd: %d\n", fd);
	}
	current_options |= O_NONBLOCK; //add nonblocking
	if(fcntl(fd, F_SETFL, current_options) == -1){
		fprintf(stderr, "Error setting options for fd: %d\n", fd);
	}
}

/* Initialize a new switch and add to the number of switches currently connected
 * to the controller.
 * Also, updates the largest file descrptor for select
 */
void setup_new_switch(int fd){
	if(idk_man.num_connected_switches >= idk_man.max_connected_switches){
		resize_switch_list();
	}
	struct of_switch *new_switch = &idk_man.switch_list[idk_man.num_connected_switches];
	
	/* add switch to the pool */
	new_switch->rw 			= WRITE; 	/* expecting a message from switch */
	new_switch->of_status 		= OFPT_HELLO;   /* expecting a hello message */
	new_switch->socket_fd 		= fd;
	new_switch->bytes_read 		= 0;
	new_switch->bytes_written 	= 0;
	new_switch->bytes_expected 	= sizeof(struct ofp_header); /* expecting hello message */
	new_switch->reading_header      = 1;
	new_switch->read_buffer_size    = 1500;
	new_switch->write_buffer_size   = 1500;
	new_switch->xid 		= xid_generator;
	new_switch->read_buffer   	= calloc(sizeof(char),  new_switch->read_buffer_size);
	new_switch->write_buffer  	= calloc(sizeof(char),  new_switch->write_buffer_size);
	new_switch->ports_requested     = 0;
	new_switch->config_set          = 0;
	new_switch->features_requested  = 0;
	new_switch->default_flow_set    = 0;
	new_switch->timeout		= 60; //set 60 second timeout
	if((new_switch->read_buffer  == NULL) || (new_switch->write_buffer == NULL)){
		fprintf(stderr, "Unable to initialize buffers for switch %lu. "
				"Exiting...\n", idk_man.num_connected_switches);
		controller_exit();
	}
	
	make_non_blocking(fd);
	idk_man.num_connected_switches++;
	xid_generator++;

	write_openflow_hello(new_switch);

	
	//check to see if we need to change largest fd
	if(fd > largest_fd){
		largest_fd = fd;
	}

	//add the switch to the network graph!
	int i = 0;
	while(network_graph->devices[i] != NULL){
		i++;
	}
	
	network_graph->devices[i] = calloc(1, sizeof(struct node));
	network_graph->devices[i]->device_num = new_switch->socket_fd; //save as socket fd for now
	network_graph->devices[i]->is_switch = 1;
	network_graph->devices[i]->next = NULL;
	network_graph->devices[i]->discovered = 0;
	network_graph->num_conn_devices++;
}

/* Handles a new connection to a switch */
void create_new_connection(){
	int new_switch_fd;

	if((new_switch_fd = accept(listening_socket_fd, NULL, NULL)) == -1){
		perror("Accept fail");
	}
	//fprintf(stderr, "FD of new switch %lu: %d\n", idk_man.num_connected_switches, new_switch_fd);
	setup_new_switch(new_switch_fd);
}

/* Handles the setup of the listening socket for our controller */
void create_listening_socket(char *char_address){
	int bind_ret = 0;

	//loop through returned link list
	for(server = matches; server != NULL; server = server->ai_next){
		//try and create a socket
		listening_socket_fd = socket(server->ai_family, server->ai_socktype, 0);	
		
		//allow us to resuse the port
		int reuse = 1;
    		if (setsockopt(listening_socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0){
        		perror("setsockopt(SO_REUSEADDR) failed");
		}
		
		
		if(listening_socket_fd == -1){
			//error creating the socket
			fprintf(stderr, "Create socket error...\n");
			continue;
		}
		
		bind_ret = bind(listening_socket_fd, server->ai_addr, server->ai_addrlen);
		if(bind_ret == -1){
			close(listening_socket_fd);
			perror("Bind error");
			continue;
		}
		//don't create and bind to more than one socket
		return;

	}
	
	fprintf(stderr, "address '%s' didn't parse (v4 or v6)\n", char_address);
	fprintf(stdout, "Server exiting cleanly.\n");
	exit(1);
}

void remake_select_sets(){
	//zero all_sockets
	FD_ZERO(&read_sockets);
	FD_ZERO(&write_sockets);
	FD_ZERO(&error_sockets);

	//initialize listening socket every time	
	FD_SET(listening_socket_fd, &read_sockets);
	FD_SET(listening_socket_fd, &error_sockets);
	
	/* loop through all connected switches */
	int i = 0; 
	for(; i < idk_man.max_connected_switches; i++){
		//if connected, decide what to do with the switch
		switch(idk_man.switch_list[i].rw){
			
			case READ :
				FD_SET(idk_man.switch_list[i].socket_fd, &read_sockets);
				break;
			
			case WRITE :
				FD_SET(idk_man.switch_list[i].socket_fd, &write_sockets);
				break;

			default : /* should be the disconnected state */
				//fprintf(stderr, "Remaking select sets, not in recognizable state!\n");
				break;
		
		}	
	}
}


/* all cases where we need to read! */
void handle_read_socket(struct of_switch *talking_switch){
	if(talking_switch->bytes_expected != 0){
		read_from(talking_switch);
		if(talking_switch->bytes_expected != 0){
			return;
		}
	}

	if(talking_switch->reading_header){
		/* read the type and how much more to expect! */
		talking_switch->of_status = ((struct ofp_header *)talking_switch->read_buffer)->type;
		talking_switch->bytes_expected = ntohs(((struct ofp_header *)talking_switch->read_buffer)->length) - sizeof(struct ofp_header);	
		talking_switch->reading_header = 0;
		if(talking_switch->bytes_expected != 0){
			//fprintf(stderr, "Reading a header... expecting %d more bytes!\n", talking_switch->bytes_expected);
			return;
		}
	}
	
	//fprintf(stderr, "Type currently: %d\n", talking_switch->of_status);
	/* At this point, the entire packet has been read in
	 * and type has been set appropriately */
	switch(talking_switch->of_status){
		case OFPT_ECHO_REQUEST : 
			/* after getting a request, respond! */
			make_echo_reply(talking_switch);
			break;	
		
		case OFPT_HELLO :
			read_openflow_hello(talking_switch);	
			break;
	
		case OFPT_FEATURES_REPLY :
			read_features(talking_switch);
			send_probe_packet(talking_switch);
			break;	
		
		case OFPT_PACKET_IN :
			read_packet_in(talking_switch);
			break;
			
		case OFPT_MULTIPART_REPLY :
			handle_multipart_reply(talking_switch);
	       		break;	       
		
		case OFPT_PORT_STATUS :
			/* an asynchronous event when link goes
			 * up or down */
			read_port_change(talking_switch);
			break;
		
		case OFPT_FLOW_REMOVED :
			//fprintf(stderr, "Flow timed out!\n");
			break;

		default:
			/* assume it is an error */
			fprintf(stderr, "This is an OFPT_ERROR! Closing connection!\n");
			//close(talking_switch->socket_fd);
			talking_switch->rw = DISCONNECTED;
			break;
	}
	//if we are not planning on writing anything, reset to read
	if(talking_switch->rw != WRITE){
		talking_switch->bytes_expected = sizeof(struct ofp_header);
		talking_switch->reading_header = 1;
		talking_switch->bytes_read     = 0;
		memset(talking_switch->read_buffer, 0, talking_switch->read_buffer_size);
	}
}

void handle_write_socket(struct of_switch *listening_switch){
	//fprintf(stderr, "Writing\n");
	if(listening_switch->bytes_expected != 0){
		write_to(listening_switch);
		if(listening_switch->bytes_expected != 0){
			return;
		}
	}
	listening_switch->bytes_written = 0;
	memset(listening_switch->write_buffer, 0, listening_switch->write_buffer_size);

	if(!listening_switch->features_requested){
		request_features(listening_switch);
		listening_switch->features_requested = 1;
	}
	else if(!listening_switch->config_set){
		set_config(listening_switch);
		listening_switch->config_set = 1;
	}
	else if(!listening_switch->ports_requested){
		get_port_info(listening_switch);
		listening_switch->ports_requested = 1;
	}
	else if(!listening_switch->default_flow_set){
		write_flow_mod(listening_switch, DEFAULT_FLOW, NULL, 0, 0);
		listening_switch->default_flow_set = 1;
	}
	else{
		//fprintf(stderr, "Reset to read next packet\n");
		listening_switch->rw = READ;
		listening_switch->bytes_expected = sizeof(struct ofp_header);
		listening_switch->reading_header = 1;
		listening_switch->bytes_read     = 0;
		listening_switch->bytes_written = 0;
	}
}

/* loop through all connected switches and split into read and write states */
void handle_all_sockets(){
	if(FD_ISSET(listening_socket_fd, &read_sockets)){
		create_new_connection();
	}
	if(FD_ISSET(listening_socket_fd, &error_sockets)){
		//fprintf(stderr, "ERROR ON LISTENING SOCKET!\n");
	}
	
	int i = 0;
	for(; i < idk_man.num_connected_switches; i++){
		if(idk_man.switch_list[i].rw != DISCONNECTED){
			if(FD_ISSET(idk_man.switch_list[i].socket_fd, &read_sockets)){
				//can read from this client
				handle_read_socket(&idk_man.switch_list[i]);
			}
			else if(FD_ISSET(idk_man.switch_list[i].socket_fd, &write_sockets)){
				//can write to this client
				handle_write_socket(&idk_man.switch_list[i]);
			}
			else{
				//fprintf(stderr, "client is alive but not set for read or write\n");
			}
		}	
	}
}

void select_loop(){
	int sel_ret = 0;

	while(!interrupt){
		//reset all of our select fd_sets
		remake_select_sets();
		
		
		//all select sets are modified in select
		sel_ret = select(largest_fd + 1, &read_sockets, &write_sockets, &error_sockets, 0);
		if(sel_ret == -1){
			//fprintf(stderr, "CALLING FROM sel_ret\n");
			//controller_exit();
		}
		else if(sel_ret == 0){
			//nothing has changed
			continue;
		}
		else{
			handle_all_sockets();
		}
	}
}

/* Initialize network graph via an adjacency list */
void initialize_network_graph(){
	network_graph = calloc(1, sizeof(struct network));
	network_graph->max_network_size = 50;
	network_graph->num_conn_devices = 0;
	network_graph->devices = calloc(network_graph->max_network_size, sizeof(struct node *));
}

/* Initializes all required memory and stats for the controller */
void initialize_controller(){
	idk_man.max_connected_switches = 50;
	idk_man.switch_list = calloc(idk_man.max_connected_switches, sizeof(struct of_switch));
}

/* returns port in host order */
int get_port(struct sockaddr *server){
	uint16_t port = 0;
	if(server->sa_family == AF_INET){
		//fprintf(stderr, "Used IPv4 binding!\n");
		port = ((struct sockaddr_in *)server)->sin_port;
	}
	else{
		//fprintf(stderr, "Used IPv6 binding!\n");
		port = ((struct sockaddr_in6 *)server)->sin6_port;
	}
	return ntohs(port);
}

void sigint_handler(int sig){
	if (SIGINT == sig){
		//fprintf(stderr, "CALLING FROM sigint_handler\n");
		controller_exit();
	}
}

int main(int argc, char **argv){
	struct sigaction sa;
	
	/* Install the signal handler */
	sa.sa_handler = sigint_handler;
	sigfillset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (-1 == sigaction(SIGINT, &sa, NULL))
	{
		perror("Couldn't set signal handler for SIGINT");
		exit(1);
	}
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family   = AF_INET; //ipv4
	hints.ai_socktype = SOCK_STREAM; //tcp 

	if(argc >= 3){
		strcpy(&controller_addr[0], argv[1]);	
		strcpy(&port[0], argv[2]);	
			
	}
	else if(argc == 2){
		strcpy(&controller_addr[0], argv[1]);	
	}	
	else{
		fprintf(stdout, "Using defaults:\n"
				"\tListening on %s:%s\n", controller_addr, port);
	}

	if((get_info_ret = getaddrinfo(controller_addr, port, &hints, &matches)) != 0){
		fprintf(stderr, "Error with addresses: %s\n", gai_strerror(get_info_ret));
	}
	
	create_listening_socket(controller_addr);
	//done binding address, so we need to free address info

	make_non_blocking(listening_socket_fd);
	largest_fd = listening_socket_fd;	
	
	//listen on server socket
	//change this 34 to a better number later!
	if(listen(listening_socket_fd, 34) != 0){
		perror("Listen failed");
		exit(1);
	}

	if(getsockname(listening_socket_fd, (struct sockaddr *)server->ai_addr, &server->ai_addrlen) == -1){
		perror("Getsockname");
		exit(1);
	}

	fprintf(stdout, "Controller listening %s:%d\n", controller_addr, get_port((struct sockaddr *)server->ai_addr));

	fflush(stdout);

	/* setup network struct */
	initialize_network_graph();
	
	/* setup the controller struct */
	initialize_controller();

	/* start looping through FDs, waiting for connections/sending packets */
	select_loop();

	/* free all used memory */	
	free_network_graph(network_graph);
	free_controller_mem();
	freeaddrinfo(server);
	close(listening_socket_fd);
	
	/* exit only when the SIGINT is received */
	exit(0);
}
