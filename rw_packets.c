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

void request_features(struct of_switch *n_switch){
	struct ofp_switch_features *feats = (struct ofp_switch_features *)n_switch->write_buffer;
	feats->header.version = OFP_VERSION;
	feats->header.type    = OFPT_FEATURES_REQUEST;
	feats->header.length  = htons(sizeof(struct ofp_header));
	feats->header.xid     = htonl(n_switch->xid++);
	
	n_switch->rw             = WRITE; /* set to write */
	n_switch->bytes_expected = ntohs(feats->header.length); /* only expect to send a header */
	fprintf(stderr, "\n\n========================\n"
			"= SENDING FEATURES REQ =\n"
			"========================\n\n");   

}

void read_features(struct of_switch *feat_switch){
	struct ofp_switch_features *feats = (struct ofp_switch_features *)feat_switch->read_buffer;
	fprintf(stderr, "Features of Switch:\n"
			"\tDatapath-id: %d\n"
			"\tNumber of Buffers: %d\n"
			"\tNumber of Tables : %d\n"
			"\tAux-id     : %d\n",
			ntohl(feats->datapath_id), ntohl(feats->n_buffers), 
			feats->n_tables, feats->auxiliary_id); 
	/* TO DO: Note that the datapath-id is a 64-bit value, upper 16-bits are implementer-designed,
	 * Lower 48 bits are mac address */

	feat_switch->bytes_read = 0;
	feat_switch->bytes_expected = sizeof(struct ofp_header);
	feat_switch->of_status  = OFPT_ERROR;
	feat_switch->rw         = READ;

}

/* OPENFLOW HELLO FUNCTIONS */
void read_openflow_hello(struct of_switch *reading_switch){	
	struct ofp_hello *ofp_hdr = (struct ofp_hello *)reading_switch->read_buffer;
	//reading_switch->xid        = ntohl(ofp_hdr->header.xid); /* save the xid of the switch */
	fprintf(stderr, "Received Openflow Hello:\n"
			"\tVersion: %d\n"
			"\tType   : %d\n"
			"\tLength : %d\n"
			"\txid    : %d\n", 
			ofp_hdr->header.version, ofp_hdr->header.type, 
			ntohs(ofp_hdr->header.length), ntohl(ofp_hdr->header.xid));

	if(ofp_hdr->header.version != OFP_VERSION){
		fprintf(stderr, "Switch not using version 1.3! Closing connection!\n");
		/* TO DO: close the connection.... */
	}

	reading_switch->bytes_read     = 0;
	request_features(reading_switch);
}

void write_openflow_hello(struct of_switch *listening_switch){
	struct ofp_hello *ofp_hdr = (struct ofp_hello *)listening_switch->write_buffer;
	ofp_hdr->header.version = OFP_VERSION;
	ofp_hdr->header.type    = OFPT_HELLO;
	ofp_hdr->header.length  = htons(sizeof(struct ofp_header));
	ofp_hdr->header.xid     = htonl(listening_switch->xid);
	fprintf(stderr, "Sending Openflow Hello:\n"
			"\tVersion: %d\n"
			"\tType   : %d\n"
			"\tLength : %d\n"
			"\txid    : %d\n", 
			ofp_hdr->header.version, ofp_hdr->header.type, 
			ntohs(ofp_hdr->header.length), ntohl(ofp_hdr->header.xid));
	
	listening_switch->rw             = WRITE; /* set to write */
	listening_switch->bytes_expected = ntohs(ofp_hdr->header.length); /* only expect to send a header */
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
	ofp_hdr_send->xid     = echo_switch->xid;

	/* memcpy over the rest of the echo_request into the write buffer */
	memcpy(echo_switch->write_buffer + sizeof(struct ofp_header),
	       echo_switch->read_buffer  + sizeof(struct ofp_header),
	       ntohs(ofp_hdr_recv->length) - sizeof(struct ofp_header));

	
	fprintf(stderr, "Writing Echo Reply:\n"
			"\tVersion: %d\n"
			"\tType   : %d\n"
			"\tLength : %d\n"
			"\txid    : %d\n", 
			ofp_hdr_send->version, ofp_hdr_send->type, ntohs(ofp_hdr_send->length), ntohl(ofp_hdr_send->xid));
	
	/* set switch to write with the appropriate number of bytes expected */	
	echo_switch->rw             = WRITE; /* set to write */
	echo_switch->bytes_expected = ntohs(ofp_hdr_send->length); /* only expect to send a header */
	echo_switch->of_status      = OFPT_ECHO_REPLY;
}

void read_echo_request(struct of_switch *echo_switch){
	struct ofp_header *ofp_hdr = (struct ofp_header *)echo_switch->read_buffer;
	fprintf(stderr, "Reading Echo Request:\n"
			"\tVersion: %d\n"
			"\tType   : %d\n"
			"\tLength : %d\n"
			"\txid    : %d\n", 
			ofp_hdr->version, ofp_hdr->type, ntohs(ofp_hdr->length), ntohl(ofp_hdr->xid));
	write_echo_reply(echo_switch);
}

void write_error(struct of_switch *e_switch){
	e_switch->rw         = WRITE; /* set to write */
	e_switch->bytes_expected = sizeof(struct ofp_header); /* only expect to send a header */
	
	struct ofp_hello *ofp_hdr = (struct ofp_hello *)e_switch->write_buffer;
	ofp_hdr->header.version = OFP_VERSION;
	ofp_hdr->header.type    = OFPT_ERROR;
	ofp_hdr->header.length  = htons(sizeof(struct ofp_header));
	ofp_hdr->header.xid     = htonl(e_switch->xid);
	fprintf(stderr, "Sending Openflow ERROR:\n"
			"\tVersion: %d\n"
			"\tType   : %d\n"
			"\tLength : %d\n"
			"\txid    : %d\n", 
			ofp_hdr->header.version, ofp_hdr->header.type, ntohs(ofp_hdr->header.length), ntohl(ofp_hdr->header.xid));
}

void read_error(struct of_switch *e_switch){
	struct ofp_error_msg *ofp_error = (struct ofp_error_msg *)e_switch->read_buffer;
	
	/* DESIGN CHOICE: I did not want to make another enum for processing a packet
	 * and then deciding where to send it. Instead, I assume the incoming packet
	 * is an error, check the type, and if it is not an error, I will change the type
	 * for the switch and pass back to the read function */
	if(ofp_error->header.type != OFPT_ERROR){
		e_switch->of_status = ofp_error->header.type;
		//e_switch->bytes_expected = ntohs(ofp_hdr->header.length) - sizeof(struct ofp_header);
		return;
	}
	
	print_of_header(ofp_error->header);	
	fprintf(stderr, "\n\tERROR MESSAGE:\n"
			"\t\tType: %d\n"
			"\t\tCode: %d\n",
			ntohs(ofp_error->type), ntohs(ofp_error->code));
	
	/* write openflow response into the write buffer */
	write_error(e_switch);
}
