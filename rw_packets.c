/* Handles input and output of openflow packets */
#include "rw_packets.h"
#include "controller.h"
#include <netinet/in.h>
#include "openflow.h"
#include "string.h"

void resize_buffer(struct of_switch *full_switch, int buffer){
	//fprintf(stderr, "\n"
	//		"========================================\n"
	//		"             RESIZING BUFFER!!!         \n"
	//		"========================================\n"
	//		"\n");
	
	char *rw_buffer = NULL;
	if(buffer == RESIZE_READ_BUFFER){
		full_switch->read_buffer_size *= 2;
		full_switch->read_buffer = realloc(full_switch->read_buffer, sizeof(char) * full_switch->read_buffer_size);
		rw_buffer = full_switch->read_buffer;
	}
	else if(buffer == RESIZE_WRITE_BUFFER){
		full_switch->write_buffer_size *= 2;
		full_switch->write_buffer = realloc(full_switch->write_buffer, sizeof(char) * full_switch->write_buffer_size);
		rw_buffer = full_switch->write_buffer;
	}
	else{
		fprintf(stderr, "What else could we possibly resize?\n");
	}

	//see if realloc was successful
	if(rw_buffer == NULL){
		fprintf(stderr, "Failed at resizing a buffer. Exiting!\n");
		exit(1);
	}	
}

void print_of_header(struct ofp_header ofp_hdr){
	fprintf(stderr, "Received Openflow Hello:\n"
			"\tVersion: %d\n"
			"\tType   : %d\n"
			"\tLength : %d\n"
			"\txid    : %d\n", 
			ofp_hdr.version, ofp_hdr.type, 
			ntohs(ofp_hdr.length), ntohl(ofp_hdr.xid));
}

void write_to(struct of_switch *w_switch){
	int bytes_sent = 0;
	int bytes_available = w_switch->write_buffer_size - w_switch->bytes_written;
	//check to see if we need to make our buffer bigger!
	if(bytes_available == 0){
		resize_buffer(w_switch, RESIZE_WRITE_BUFFER);
		bytes_available = w_switch->read_buffer_size - w_switch->bytes_read;
	}
	
	bytes_sent = send(w_switch->socket_fd, w_switch->write_buffer + w_switch->bytes_written, w_switch->bytes_expected, 0); 
	w_switch->bytes_written  += bytes_sent;
	w_switch->bytes_expected -= bytes_sent;
	//fprintf(stderr, "Total bytes written to socket fd %d so far: %d\n", w_switch->socket_fd, w_switch->bytes_written);
}

void read_from(struct of_switch *hi_switch){
	int bytes_received = 0;
	int bytes_available = hi_switch->read_buffer_size - hi_switch->bytes_read;
	//check to see if we need to make our buffer bigger!
	if(bytes_available == 0){
		resize_buffer(hi_switch, RESIZE_READ_BUFFER);
		bytes_available = hi_switch->read_buffer_size - hi_switch->bytes_read;
	}
	
	bytes_received = recv(hi_switch->socket_fd, hi_switch->read_buffer + hi_switch->bytes_read, 
			      hi_switch->bytes_expected, 0); 
	hi_switch->bytes_read     += bytes_received;
	hi_switch->bytes_expected -= bytes_received;
	//fprintf(stderr, "Total bytes read from socket fd %d so far: %d\n", hi_switch->socket_fd, hi_switch->bytes_read);
}


void set_config(struct of_switch *lost_switch){
	struct ofp_switch_config *config = (struct ofp_switch_config *)lost_switch->write_buffer;
	config->header.version = OFP_VERSION;
	config->header.type    = OFPT_SET_CONFIG;
	config->header.length  = htons(sizeof(struct ofp_switch_config)); /* 12 bytes */ 
	config->header.xid     = htonl(lost_switch->xid++);

	config->flags          = OFPC_FRAG_NORMAL;
	config->miss_send_len  = htons(128); /* tbh idk what this value is yet */

	lost_switch->of_status      = OFPT_SET_CONFIG;
	lost_switch->bytes_expected = sizeof(struct ofp_switch_config);
        lost_switch->rw             = WRITE;	
}


void read_config(struct of_switch *set_switch){
	struct ofp_switch_config *config = (struct ofp_switch_config *)set_switch->read_buffer;
	fprintf(stderr, "CONFIG ON SWITCH:\n"
			"\tFlags           : 0x%08x\n"
			"\tMiss Send Length: %d\n",
			config->flags, ntohs(config->miss_send_len));

	set_switch->bytes_expected = sizeof(struct ofp_switch_config);
	set_switch->rw             = READ;	

}	

void request_features(struct of_switch *n_switch){
	struct ofp_switch_features *feats = (struct ofp_switch_features *)n_switch->write_buffer;
	feats->header.version = OFP_VERSION;
	feats->header.type    = OFPT_FEATURES_REQUEST;
	feats->header.length  = htons(sizeof(struct ofp_header));
	feats->header.xid     = htonl(n_switch->xid++);
	
	n_switch->bytes_expected = ntohs(feats->header.length); /* only expect to send a header */
	//fprintf(stderr, "\n\n========================\n"
	//		"= SENDING FEATURES REQ =\n"
	//		"========================\n\n");   

	/* set controller to write feature request to switch */
	n_switch->of_status = OFPT_FEATURES_REQUEST;
	n_switch->bytes_expected = sizeof(struct ofp_header);
	n_switch->rw = WRITE;
}

void read_features(struct of_switch *feat_switch){
	//struct ofp_switch_features *feats = (struct ofp_switch_features *)feat_switch->read_buffer;
	//fprintf(stderr, "Features of Switch:\n"
	//		"\tDatapath-id      : %d\n"
	//		"\tNumber of Buffers: %d\n"
	//		"\tNumber of Tables : %d\n"
	//		"\tAux-id           : %d\n"
	//		"\tCapabilities     : 0x%08x\n",
	//		ntohl(feats->datapath_id), ntohl(feats->n_buffers), 
	//		feats->n_tables, feats->auxiliary_id, ntohl(feats->capabilities)); 
	/* TO DO: Note that the datapath-id is a 64-bit value, upper 16-bits are implementer-designed,
	 * Lower 48 bits are mac address */
	
	/* set controller to listen more to switch */
	feat_switch->rw = READ;
	feat_switch->bytes_expected = sizeof(struct ofp_header);
	feat_switch->reading_header = 1;
}

/* OPENFLOW HELLO FUNCTIONS */
void read_openflow_hello(struct of_switch *reading_switch){	
	struct ofp_hello *ofp_hdr = (struct ofp_hello *)reading_switch->read_buffer;
	
	if(ofp_hdr->header.version != OFP_VERSION){
		fprintf(stderr, "Switch not using version 1.3! Closing connection!\n");
		/* TO DO: close the connection.... */
	}
	
	/* after reading hello, listen more to switch */
	reading_switch->rw = READ;
	reading_switch->bytes_expected = sizeof(struct ofp_header);
	reading_switch->reading_header = 1;
}

void write_openflow_hello(struct of_switch *listening_switch){
	struct ofp_hello *ofp_hdr = (struct ofp_hello *)listening_switch->write_buffer;
	ofp_hdr->header.version = OFP_VERSION;
	ofp_hdr->header.type    = OFPT_HELLO;
	ofp_hdr->header.length  = htons(sizeof(struct ofp_header));
	ofp_hdr->header.xid     = htonl(listening_switch->xid++);

	listening_switch->rw = WRITE;
	listening_switch->of_status = OFPT_HELLO;
	listening_switch->bytes_expected = sizeof(struct ofp_header);	
}
/* END OPENFLOW HELLO FUNCTIONS */

void write_echo_request(struct of_switch *echo_switch);
void read_echo_reply(struct of_switch *echo_switch);

void write_echo_reply(struct of_switch *echo_switch){
	struct ofp_header *ofp_hdr_recv = (struct ofp_header *)echo_switch->read_buffer;
	struct ofp_header *ofp_hdr_send = (struct ofp_header *)echo_switch->write_buffer;
	ofp_hdr_send->version = OFP_VERSION;
	ofp_hdr_send->type    = OFPT_ECHO_REPLY;
	ofp_hdr_send->length  = ofp_hdr_recv->length; /* send as much as received */
	ofp_hdr_send->xid     = ofp_hdr_recv->xid; /* send back the same xid as request */

	/* memcpy over the rest of the echo_request into the write buffer */
	if(ntohs(ofp_hdr_recv->length) - sizeof(struct ofp_header) != 0){
		memcpy(echo_switch->write_buffer + sizeof(struct ofp_header),
	       	       echo_switch->read_buffer  + sizeof(struct ofp_header),
	       	       ntohs(ofp_hdr_recv->length) - sizeof(struct ofp_header));
	}
	
	echo_switch->rw = WRITE;
	echo_switch->of_status = OFPT_ECHO_REPLY;
	echo_switch->bytes_expected = ntohs(ofp_hdr_send->length); /* only expect to send a header */
}

/* should never have to read an echo request (unless it's malformed).
 * If malformed, theres a bigger problem */
void read_echo_request(struct of_switch *echo_switch){
	//struct ofp_header *ofp_hdr = (struct ofp_header *)echo_switch->read_buffer;
	//fprintf(stderr, "Reading Echo Request:\n"
	//		"\tVersion: %d\n"
	//		"\tType   : %d\n"
	//		"\tLength : %d\n"
	//		"\txid    : %d\n", 
	//		ofp_hdr->version, ofp_hdr->type, ntohs(ofp_hdr->length), ntohl(ofp_hdr->xid));
}

void write_error(struct of_switch *e_switch){
	e_switch->rw         = WRITE; /* set to write */
	e_switch->bytes_expected = sizeof(struct ofp_header); /* only expect to send a header */
	
	struct ofp_hello *ofp_hdr = (struct ofp_hello *)e_switch->write_buffer;
	ofp_hdr->header.version = OFP_VERSION;
	ofp_hdr->header.type    = OFPT_ERROR;
	ofp_hdr->header.length  = htons(sizeof(struct ofp_header));
	ofp_hdr->header.xid     = htonl(e_switch->xid++);
	fprintf(stderr, "Sending Openflow ERROR:\n"
			"\tVersion: %d\n"
			"\tType   : %d\n"
			"\tLength : %d\n"
			"\txid    : %d\n", 
			ofp_hdr->header.version, ofp_hdr->header.type, ntohs(ofp_hdr->header.length), ntohl(ofp_hdr->header.xid));
}

void read_error(struct of_switch *e_switch){
	struct ofp_error_msg *ofp_error = (struct ofp_error_msg *)e_switch->read_buffer;
	
	print_of_header(ofp_error->header);	
	fprintf(stderr, "\n\tERROR MESSAGE:\n"
			"\t\tType: %d\n"
			"\t\tCode: %d\n",
			ntohs(ofp_error->type), ntohs(ofp_error->code));
	
	/* write openflow response into the write buffer */
	write_error(e_switch);
}


/* multipart request to get all the data for all ports on
 * connected switch */
void get_port_info(struct of_switch *unk_switch){
	struct ofp_multipart_request *multi = (struct ofp_multipart_request *)unk_switch->write_buffer;
	multi->header.version = OFP_VERSION;
	multi->header.type    = OFPT_MULTIPART_REQUEST;
	multi->header.length  = htons(sizeof(struct ofp_multipart_request) + sizeof(struct ofp_port_stats_request));
	multi->header.xid     = htonl(unk_switch->xid++);

	multi->type = htons(OFPMP_PORT_STATS); 
	multi->flags = htons(0); //lol
	
	/* want port stats for all ports on switch */
	struct ofp_port_stats_request *stats = (struct ofp_port_stats_request *)multi->body;
	stats->port_no = htonl(OFPP_ANY);
	
	
	unk_switch->rw = WRITE;
	unk_switch->of_status = OFPT_MULTIPART_REQUEST;
	unk_switch->bytes_expected = ntohs(multi->header.length); /* only expect to send a header */
}

void write_flow_mod(struct of_switch *needs_help){

}

void read_packet_in(struct of_switch *r_switch){
	struct ofp_packet_in *pkt = (struct ofp_packet_in *)r_switch->read_buffer;
	fprintf(stderr, "\n=================="
			"\n=== Packet in ==="
			"\n================="
			"\tBuffer ID: 0x%08x\n"
			"\tTotal Len: %d\n"
			"\tReason   : %d\n"
			"\tTable ID : %d"
			"\n=================\n\n",
			ntohl(pkt->buffer_id), ntohs(pkt->total_len),
			pkt->reason, pkt->table_id);
	
	/* TO DO: Currently when a packet comes in, we read it in, 
	 * and then do nothing with it! */
	r_switch->rw = READ;
	r_switch->bytes_expected = sizeof(struct ofp_header);
	r_switch->reading_header = 1;
}


void handle_multipart_reply(struct of_switch *loaded_switch){
	struct ofp_multipart_reply *multi = (struct ofp_multipart_reply *)loaded_switch->read_buffer;
	
	switch(ntohs(multi->type)){
		case OFPMP_PORT_STATS :
			//fprintf(stderr, "Multipart message concerning ports!\n");
			break;

		default :
			//fprintf(stderr, "Multipart Message not supported yet!\n");
			break;
	
	}
	
	/* after reading hello, listen more to switch */
	loaded_switch->rw = READ;
	loaded_switch->bytes_expected = sizeof(struct ofp_header);
	loaded_switch->reading_header = 1;
}
