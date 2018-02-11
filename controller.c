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


/* GLOBAL DEFS */
struct addrinfo hints, *matches, *server;
int get_info_ret;

struct of_controller idk_man; 		/* struct to hold all controller info */

char port[15] = "6653"; 		/* common port for Openflow */
char controller_addr[15] = "0.0.0.0"; /* usually run locally */

int listening_socket_fd = 0; 		/* holds the fd for listening socket */
int largest_fd = 1; 			/* held for select loop */

int interrupt = 0; 			/* for SIGINT */

int xid_generator = 1;

fd_set read_sockets;
fd_set write_sockets;
fd_set error_sockets;
/* END GLOBAL DEFS */

/* Free all buffers for connected and used switches */
void free_switch_buffers(struct of_switch *switches){

}

/* free all allocated memory held by the controller */
void free_controller_mem(){
	free_switch_buffers(idk_man.switch_list);
	free(idk_man.switch_list);
}

/* when SIGINT is received, this is called to clean up */
void controller_exit(){
	free_controller_mem();
	interrupt = 1; 
}

/* Called when there are more switches that need to be connected to the controller 
 * It is highly unlikely we will have more than 100 switches connected to the controller
 */
void resize_switch_list(){
	idk_man.max_connected_switches *= 2;
	idk_man.switch_list = realloc(idk_man.switch_list, sizeof(struct of_switch) * idk_man.max_connected_switches);
	if(idk_man.switch_list == NULL){
		fprintf(stderr, "Unable to hold info for %d switches... \n"
				"\n=====================================\n"
				"\n  OPENFLOW CONTROLLER SHUTTING DOWN  \n"
				"\n=====================================\n", idk_man.max_connected_switches);
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
	new_switch->rw 			= READ; 	/* expecting a message from switch */
	new_switch->of_status 		= OFPT_ERROR;   /* expecting a hello message */
	new_switch->socket_fd 		= fd;
	new_switch->bytes_read 		= 0;
	new_switch->bytes_written 	= 0;
	new_switch->bytes_expected 	= sizeof(struct ofp_header); /* expecting hello message */
	new_switch->read_buffer_size    = 1500;
	new_switch->write_buffer_size   = 1500;
	new_switch->xid 		= xid_generator;
	new_switch->read_buffer   	= calloc(sizeof(char),  new_switch->read_buffer_size);
	new_switch->write_buffer  	= calloc(sizeof(char),  new_switch->write_buffer_size);
	if((new_switch->read_buffer  == NULL) || (new_switch->write_buffer == NULL)){
		fprintf(stderr, "Unable to initialize buffers for switch %lu. "
				"Exiting...\n", idk_man.num_connected_switches);
		controller_exit();
	}
	
	idk_man.num_connected_switches++;
	xid_generator++;
	
	write_openflow_hello(new_switch);	
	
	//check to see if we need to change largest fd
	if(fd > largest_fd){
		largest_fd = fd;
	}
}

/* Handles a new connection to a switch */
void create_new_connection(){
	int new_switch_fd;

	new_switch_fd = accept(listening_socket_fd, NULL, NULL);
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
			//fprintf(stderr, "Create socket error...\n");
			continue;
		}
		if(char_address == NULL){ //not passed an address
			//turn off the IPv6 Only option
			int no = 0;
			//fprintf(stderr, "Setting socket options!\n");
			setsockopt(listening_socket_fd, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&no, sizeof(no)); 
		}
		//fprintf(stderr, "Designated FD for listening socket: %d\n", listening_socket_fd);
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
		return;
	}
	/* each function called here will appropriately
	 * change the status from READ -> WRITE 
	 */
	switch(talking_switch->of_status){
		case OFPT_HELLO :
			//fprintf(stderr, "We are reading an OFPT_HELLO!\n");
			read_openflow_hello(talking_switch);	
			break;
		
		case OFPT_ECHO_REQUEST : 
			//fprintf(stderr, "We are reading an OFPT_ECHO_REQUEST!\n");
			read_echo_request(talking_switch);
			break;	
	
		case OFPT_FEATURES_REPLY :
			read_features(talking_switch);
		
		default:
			/* assume it is an error at first */
			//fprintf(stderr, "Default is OFPT_ERROR!\n");
			read_error(talking_switch);
			break;
	}

}

void handle_write_socket(struct of_switch *listening_switch){
	if(listening_switch->bytes_expected != 0){
		write_to(listening_switch);
		return;
	}
	switch(listening_switch->of_status){
		case OFPT_HELLO :
			//fprintf(stderr, "We are reading an OFPT_HELLO!\n");
			break;
		
		case OFPT_ECHO_REPLY : 
			fprintf(stderr, "finsihed reading an OFPT_ECHO_REPLY!\n");
			break;	
		
		case OFPT_FEATURES_REQUEST :
			fprintf(stderr, "Finished writing an OFPT_FEATURES_REQUEST!\n");
			break;	
		
		default:
			/* assume it is an error at first */
			//fprintf(stderr, "Default is OFPT_ERROR!\n");
			break;
	}
	
	/* finished sending a packet, can expect an openflow header next! */
	//fprintf(stderr, "Resetting to listen for next packet!\n");
	listening_switch->bytes_expected = sizeof(struct ofp_header);
	listening_switch->rw             = READ;
	listening_switch->of_status      = OFPT_ERROR;
	listening_switch->bytes_read     = 0; //reset read
	listening_switch->bytes_written  = 0; //reset write
}

/* loop through all connected switches and split into read and write states */
void handle_all_sockets(){
	if(FD_ISSET(listening_socket_fd, &read_sockets)){
		create_new_connection();
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
			controller_exit();
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

/* Initializes all required memory and stats for the controller */
void initialize_controller(){
	idk_man.max_connected_switches = 100;
	idk_man.switch_list = calloc(idk_man.max_connected_switches, sizeof(struct of_switch));
	
	//initialize all to disconnected state
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

	fprintf(stdout, "Controller listening on TCP port %d\n", get_port((struct sockaddr *)server->ai_addr));

	fflush(stdout);

	/* setup the controller struct */
	initialize_controller();

	/* start looping through FDs, waiting for connections/sending packets */
	select_loop();

	/* exit only when the SIGINT is received */
	exit(0);
}
