/********************************************
 *
 * Flows.c handles packet in processing and 
 * formulates the correct packet out to send
 * to the questioning switch
 *
 *******************************************/


#include "openflow.h"
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <netinet/ether.h>
#include "controller.h"

/* packet length is set by calling function to just the length
 * of the ethernet frame
 */
void process_ethernet_frame(struct of_switch *s){
	struct ether_header *pkt = (struct ether_header *)s->read_buffer;	
	fprintf(stderr, "Ethernet Packet:\n"
			"\tSRC : %s\n"
			"\tDST : %s\n"
			"\tType: %d\n",
			ether_ntoa((struct ether_addr *)pkt->ether_shost), 
			ether_ntoa((struct ether_addr *)pkt->ether_dhost), ntohs(pkt->ether_type));
}

void default_miss(void){


}
