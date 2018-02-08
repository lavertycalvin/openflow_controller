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
#include "controller.h"


/* GLOBAL DEFS */
struct addrinfo hints, *matches, *server;
int get_info_ret;

struct of_controller idk_man; 		/* struct to hold all controller info */

char port[15] = "6653"; 		/* common port for Openflow */
char controller_addr[15] = "127.0.0.1"; /* usually run locally */

int listening_socket_fd = 0; 		/* holds the fd for listening socket */
int largest_fd = 1; 			/* held for select loop */

int interrupt = 0; 			/* for SIGINT */

fd_set read_sockets;
fd_set write_sockets;
fd_set error_sockets;
/* END GLOBAL DEFS */

/* free all allocated memory held by the controller */
void free_controller_mem(){
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

void setup_new_switch(int fd){
	if(idk_man.num_connected_switches >= idk_man.max_connected_switches){
		resize_switch_list();
	}
	struct of_switch *new_switch = &idk_man.switch_list[idk_man.num_connected_switches];
	
	/* add switch to the pool */
	new_switch->is_alive		= 1;
	new_switch->socket_fd 		= fd;
	new_switch->bytes_read 		= 0;
	new_switch->bytes_written 	= 0;
	new_switch->read_buffer_size    = 1500;
	new_switch->write_buffer_size   = 1500;
	new_switch->read_buffer   	= calloc(sizeof(char),  new_switch->read_buffer_size);
	new_switch->write_buffer  	= calloc(sizeof(char),  new_switch->write_buffer_size);
	if((new_switch->read_buffer  == NULL) || (new_switch->write_buffer == NULL)){
		fprintf(stderr, "Unable to initialize buffers for switch %lu. "
				"Exiting...\n", idk_man.num_connected_switches);
		controller_exit();
	}
	
	idk_man.num_connected_switches++;
	
	//check to see if we need to change largest fd
	if(fd > largest_fd){
		largest_fd = fd;
	}
}


/* Handles a new connection to a switch */
void create_new_connection(){
	int new_switch_fd;

	new_switch_fd = accept(listening_socket_fd, NULL, NULL);
	fprintf(stderr, "FD of switch %lu: %d\n", idk_man.num_connected_switches, new_switch_fd);
	//now we need to send OFP_HELLO to the connected switch 
	setup_new_switch(new_switch_fd);
}

/* Handles the setup of the listening socket for our controller */
void create_listening_socket(char *char_address){
	int bind_ret = 0;

	//loop through returned link list
	for(server = matches; server != NULL; server = server->ai_next){
		//try and create a socket
		listening_socket_fd = socket(server->ai_family, server->ai_socktype, 0);	
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
}

void handle_all_sockets(){
	if(FD_ISSET(listening_socket_fd, &read_sockets)){
		fprintf(stderr, "New Connection!\n");
		create_new_connection();
	}
	
	int i = 0;
	for(; i < idk_man.num_connected_switches; i++){
		if(idk_man.switch_list[i].is_alive){
			if(FD_ISSET(idk_man.switch_list[i].socket_fd, &read_sockets)){
				//can read from this client
			}
			else if(FD_ISSET(idk_man.switch_list[i].socket_fd, &write_sockets)){
				//can write to this client	
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
		//fprintf(stderr, "SOMETHING HAS CHANGED!\n");
		if(sel_ret == -1){
			controller_exit();
		}
		else if(sel_ret == 0){
			//nothing has changed
			continue;
		}
		else{
			//we can do things!
			//fprintf(stderr, "HANDLING ALL SOCKETS!\n");
			handle_all_sockets();
		}
	}
}

/* Initializes all required memory and stats for the controller */
void initialize_controller(){
	idk_man.max_connected_switches = 100;
	idk_man.switch_list = malloc(idk_man.max_connected_switches * sizeof(struct of_switch));
}

/* returns port in host order */
int get_port(struct sockaddr *server){
	uint16_t port = 0;
	if(server->sa_family == AF_INET){
		fprintf(stderr, "Used IPv4 binding!\n");
		port = ((struct sockaddr_in *)server)->sin_port;
	}
	else{
		fprintf(stderr, "Used IPv6 binding!\n");
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

	fprintf(stdout, "HTTP server is using TCP port %d\n", get_port((struct sockaddr *)server->ai_addr));

	fflush(stdout);

	/* setup the controller struct */
	initialize_controller();

	/* start looping through FDs, waiting for connections/sending packets */
	select_loop();

	/* exit only when the SIGINT is received */
	exit(0);
}
