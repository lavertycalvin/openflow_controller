/* Handles input and output of openflow packets */

#include "rw_packets.h"
#include "controller.h"
#include <netinet/in.h>
#include <netinet/ether.h>
#include "openflow.h"
#include "string.h"
#include <unistd.h>

struct ether_addr probe_packet_ether_addr = {{33,33,33,33,33,33}}; //current hack to check switch to switch links

struct probe_packet probe = { .e = {.dest = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
				    .source = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
				    .type = 0},
			    };




void resize_buffer(struct of_switch *full_switch, int buffer){
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
	//fprintf(stderr, "Received Openflow Header:\n"
	//		"\tVersion: %d\n"
	//		"\tType   : %d\n"
	//		"\tLength : %d\n"
	//		"\txid    : %d\n", 
	//		ofp_hdr.version, ofp_hdr.type, 
	//		ntohs(ofp_hdr.length), ntohl(ofp_hdr.xid));
}

void write_to(struct of_switch *w_switch){
	int bytes_sent = 0;
	int bytes_available = w_switch->write_buffer_size - w_switch->bytes_written;
	//check to see if we need to make our buffer bigger!
	if(bytes_available == 0){
		resize_buffer(w_switch, RESIZE_WRITE_BUFFER);
		bytes_available = w_switch->read_buffer_size - w_switch->bytes_read;
	}

	bytes_sent = send(w_switch->socket_fd, w_switch->write_buffer + w_switch->bytes_written, w_switch->bytes_expected, MSG_DONTWAIT); 
	if(bytes_sent < 0){
		perror("send");
	}
	w_switch->bytes_written  += bytes_sent;
	w_switch->bytes_expected -= bytes_sent;
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
			      hi_switch->bytes_expected, MSG_DONTWAIT); 
	if(bytes_received < 0){
		perror("recv");
	}
	hi_switch->bytes_read     += bytes_received;
	hi_switch->bytes_expected -= bytes_received;
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
	set_switch->reading_header = 1;
	set_switch->bytes_read     = 0;

}	

void request_features(struct of_switch *n_switch){
	struct ofp_switch_features *feats = (struct ofp_switch_features *)n_switch->write_buffer;
	feats->header.version = OFP_VERSION;
	feats->header.type    = OFPT_FEATURES_REQUEST;
	feats->header.length  = htons(sizeof(struct ofp_header));
	feats->header.xid     = htonl(n_switch->xid++);
	
	n_switch->bytes_expected = ntohs(feats->header.length); /* only expect to send a header */
	
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
	//feat_switch->rw = READ;
	//feat_switch->bytes_expected = sizeof(struct ofp_header);
	//feat_switch->reading_header = 1;
	//feat_switch->bytes_read = 0;
}

/* OPENFLOW HELLO FUNCTIONS */
void read_openflow_hello(struct of_switch *reading_switch){	
	struct ofp_hello *ofp_hdr = (struct ofp_hello *)reading_switch->read_buffer;

	//reading_switch->xid = ntohl(ofp_hdr->header.xid);
	if(ofp_hdr->header.version != OFP_VERSION){
		fprintf(stderr, "Switch not using version 1.3! Closing connection!\n");
		
		/* TO DO: close the connection.... */
		close(reading_switch->socket_fd);
	}
	else{
		//fprintf(stderr, "Compatible Switch running Openflow v1.3\n");
	}
}

void write_openflow_hello(struct of_switch *listening_switch){
	struct ofp_hello *ofp_hdr = (struct ofp_hello *)listening_switch->write_buffer;
	ofp_hdr->header.version = OFP_VERSION;
	ofp_hdr->header.type    = OFPT_HELLO;
	ofp_hdr->header.length  = htons(sizeof(struct ofp_header) + sizeof(struct ofp_hello_elem_versionbitmap) + 4);
	ofp_hdr->header.xid     = htonl(listening_switch->xid++);

	ofp_hdr->elements[0].type = htons(OFPHET_VERSIONBITMAP); 
	ofp_hdr->elements[0].length = htons(sizeof(struct ofp_hello_elem_versionbitmap) + 4);
	struct ofp_hello_elem_versionbitmap *bitmap = (struct ofp_hello_elem_versionbitmap *)&ofp_hdr->elements[0].type;
	bitmap->bitmaps[0] = htonl(1 << 4);

	listening_switch->rw = WRITE;
	listening_switch->bytes_expected = sizeof(struct ofp_hello) + sizeof(struct ofp_hello_elem_versionbitmap) + 4;	
}
/* END OPENFLOW HELLO FUNCTIONS */

void write_echo_request(struct of_switch *echo_switch);
void read_echo_reply(struct of_switch *echo_switch);


void read_port_change(struct of_switch *uneasy_switch, struct network *graph){
	struct ofp_port_status *status = (struct ofp_port_status *)uneasy_switch->read_buffer;
	struct ofp_port *port = (struct ofp_port *)&status->desc;
	fprintf(stderr, "\nXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
			"\nXXXXXXXXX PORT CHANGE! OH NOOOOOO XXXXXXXXXXX"
			"\nXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
			"\n\tSwitch Number    : %d"
			"\n\tPort that changed: %u"
			"\n\tName  of port    : %s"
			"\n\tState of port    : %u (0 up, 1 down)"
			"\nXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n\n",
			uneasy_switch->socket_fd, ntohl(port->port_no), port->name, ntohl(port->state));
	
	/******************************************
	 * TO DO: change the network to mark the 
	 * 	  port as unusable
	 ******************************************/
	int i = 0;
	while(graph->devices[i]->device_num != uneasy_switch->socket_fd) i++;
	struct node *next_node = graph->devices[i]->next;
	while(next_node != NULL){
		if(next_node->port_num == ntohl(port->port_no)){
			if(ntohl(port->state == 0)){
				fprintf(stderr, "Switch to switch connection established\n");
			}
			else{
				fprintf(stderr, "Switch to switch connection has dropped!\n");
				/***************************************************
				 * When this happens, check to see if we previously 
				 * 	had a loop. If we did, re-establish the link
				 ***************************************************/
			}
		}
		next_node = next_node->next;
	}
	
}

void make_echo_reply(struct of_switch *echo_switch){
	//fprintf(stderr, "Writing echo reply!\n");
	struct ofp_header *ofp_hdr_recv = (struct ofp_header *)echo_switch->read_buffer;
	struct ofp_header *ofp_hdr_send = (struct ofp_header *)echo_switch->write_buffer;
	ofp_hdr_send->version = OFP_VERSION;
	ofp_hdr_send->type    = OFPT_ECHO_REPLY;
	ofp_hdr_send->length  = ofp_hdr_recv->length; /* send as much as received */
	ofp_hdr_send->xid     = ofp_hdr_recv->xid; /* send back the same xid as request */

	/* memcpy over the rest of the echo_request into the write buffer */
	if(ntohs(ofp_hdr_recv->length) - 8 != 0){
		memcpy(echo_switch->write_buffer + sizeof(struct ofp_header),
	       	       echo_switch->read_buffer  + sizeof(struct ofp_header),
	       	       ntohs(ofp_hdr_recv->length) - sizeof(struct ofp_header));
	}
	echo_switch->bytes_expected = ntohs(ofp_hdr_recv->length);
	echo_switch->bytes_read = 0;
	echo_switch->rw = WRITE;
}

/* should never have to read an echo request (unless it's malformed).
 * If malformed, theres a bigger problem */
void read_echo_request(struct of_switch *echo_switch);

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
	multi->header.length  = htons(sizeof(struct ofp_multipart_request));
	multi->header.xid     = htonl(unk_switch->xid++);

	multi->type = htons(OFPMP_PORT_DESC); 
	multi->flags = htons(0); //lol
	
	unk_switch->rw = WRITE;
	unk_switch->of_status = OFPT_MULTIPART_REQUEST;
	unk_switch->bytes_expected = ntohs(multi->header.length); /* only expect to send a header */
}

void flood_packet(struct of_switch *s, uint32_t buffer_id){
	//send packet out
	//fprintf(stderr, "Flooding packet!\n");
	struct ofp_packet_out *out = (struct ofp_packet_out *)s->write_buffer;
	out->header.type    = OFPT_PACKET_OUT;
	out->header.length  = htons(sizeof(struct ofp_packet_out) + sizeof(struct ofp_action_output));
	out->header.xid     = htonl(s->xid++);
	out->header.version = OFP_VERSION;
	

	out->buffer_id   = htonl(buffer_id); //use the buffer id that corresponds to the packet
	out->in_port     = htonl(OFPP_CONTROLLER);
        out->actions_len = htons(sizeof(struct ofp_action_output));
	
	//set the packet to flood
	struct ofp_action_output *action = (struct ofp_action_output *)out->actions;
	action->type    = htons(OFPAT_OUTPUT);
	action->len     = htons(sizeof(struct ofp_action_output)); 
	action->max_len = htons(64000); // set default size to 64k
	action->port    = htonl(OFPP_FLOOD); //flood the packet
	
	s->rw = WRITE;
	s->of_status = OFPT_PACKET_OUT;
	s->bytes_expected = ntohs(out->header.length);
	s->bytes_read     = 0;
}


/***************************************
 * When a new switch is connected, send out a packet to each
 * connected port that is active to check if any connections
 * are switch-switch links
 * When the controller floods the packet, the data
 * included in the packet is the pointer of the switch that is flooding
 *
 ***************************************/
void send_probe_packet(struct of_switch *new_s){
	//send packet out
	//fprintf(stderr, "Sending out probe packet from switch %d\n", new_s->socket_fd);
	struct ofp_packet_out *out = (struct ofp_packet_out *)new_s->write_buffer;
	out->header.type    = OFPT_PACKET_OUT;
	out->header.length  = htons(sizeof(struct ofp_packet_out) + sizeof(struct ofp_action_output) + sizeof(struct probe_packet));
	out->header.xid     = htonl(new_s->xid++);
	out->header.version = OFP_VERSION;
	
	out->buffer_id   = htonl(OFP_NO_BUFFER);
	out->in_port     = htonl(OFPP_CONTROLLER);
        out->actions_len = htons(sizeof(struct ofp_action_output));
	
	//set the packet to flood
	struct ofp_action_output *action = (struct ofp_action_output *)out->actions;
	action->type    = htons(OFPAT_OUTPUT);
	action->len     = htons(sizeof(struct ofp_action_output));
	action->max_len = htons(OFPCML_NO_BUFFER);
	action->port    = htonl(OFPP_FLOOD);

	//now we add on the probe packet
	char *data = (char *)&new_s->write_buffer[ntohs(out->header.length) - sizeof(struct probe_packet)];
	//fprintf(stderr, "Socket fd is %d\n", new_s->socket_fd);
	probe.e.type = htons(new_s->socket_fd);
	memcpy(data, &probe, sizeof(struct probe_packet));	
	
	//and direct to send
	new_s->rw = WRITE;
	new_s->of_status = OFPT_PACKET_OUT;
	new_s->bytes_expected = ntohs(out->header.length);
}

/* makes sure that the number passed by lenght of match is a multiple of 16 */
int round_up(int num){
	if(num % 8 == 0){
		return num;
	}
	else{
		num /= 8;
		num++;
		return num * 8;
	}

}

/* handles initial setup of broadcast and default miss rules */
void write_default_miss(struct of_switch *needs_help, int broadcast_rule){
	struct ofp_flow_mod *mod = (struct ofp_flow_mod *)&needs_help->write_buffer[needs_help->bytes_expected];
	mod->header.type    = OFPT_FLOW_MOD;
	mod->header.version = OFP_VERSION;
	mod->header.xid     = htonl(needs_help->xid++);
	
	if(broadcast_rule){
		mod->table_id = DST_TABLE;
	}
	else{
		mod->table_id = SRC_TABLE; //apply default miss to first table
	}
	mod->cookie       = 0;
	mod->cookie_mask  = 0;
	mod->command      = OFPFC_ADD;
	mod->idle_timeout = htons(0); //never time out
	mod->hard_timeout = htons(0); //never time out
	mod->priority     = htons(0); // lowest priority
	mod->buffer_id    = htonl(OFP_NO_BUFFER); //not applied to buffered packet

	mod->out_port     = 0;
	mod->out_group    = 0;
	mod->flags        = htons(OFPFF_SEND_FLOW_REM | OFPFF_CHECK_OVERLAP | OFPFF_RESET_COUNTS);
	mod->match.type   = htons(OFPMT_OXM);
	
	struct ofp_instruction_actions *instr;
	struct ofp_action_output *action;
	
	if(broadcast_rule){
		//now add all the oxm fields for dst (should be matching on ether addr)
		uint32_t *oxm_fields = (uint32_t *)mod->match.oxm_fields;	
		oxm_fields[0] = htonl(OXM_OF_ETH_DST);
		uint8_t *oxm_ether_addr	= (uint8_t *)&oxm_fields[1];
		int i = 0;
		for(; i < 6; i++){
			oxm_ether_addr[i] = 0xff; //set to match on broadcast dest
		}
		
		mod->match.length = htons(14);	//2 length, 2 type, 10 for dst addr
		instr = (struct ofp_instruction_actions *)&needs_help->write_buffer[needs_help->bytes_expected + sizeof(struct ofp_flow_mod) + round_up(ntohs(mod->match.length)) - sizeof(struct ofp_match)];
		instr->len = htons(sizeof(struct ofp_instruction_actions) + sizeof(struct ofp_action_output));
		action = (struct ofp_action_output *)instr->actions;
		action->port = htonl(OFPP_FLOOD);
		mod->header.length  = htons(sizeof(struct ofp_instruction_actions) + sizeof(struct ofp_flow_mod) + round_up(ntohs(mod->match.length) - sizeof(struct ofp_match) + sizeof(struct ofp_action_output)));	
	}
	else{
		instr = (struct ofp_instruction_actions *)&needs_help->write_buffer[needs_help->bytes_expected + sizeof(struct ofp_flow_mod)];
		action = (struct ofp_action_output *)instr->actions;

		mod->match.length = htons(4); //2 for type, 2 for length (but instruction comes 8 bytes after start)

		//now add the isntruction to the end of the match 
		instr->len  = htons(sizeof(struct ofp_instruction_actions) + sizeof(struct ofp_action_output));

		//now specify the action (forward to controller)
		action->port = htonl(OFPP_CONTROLLER);
		mod->header.length  = htons(sizeof(struct ofp_flow_mod) + sizeof(struct ofp_instruction_actions) + sizeof(struct ofp_action_output));	
	}
	action->len  = htons(sizeof(struct ofp_action_output));
	action->type = htons(OFPAT_OUTPUT);	
	action->max_len = htons(OFPCML_NO_BUFFER);	
	instr->type = htons(OFPIT_APPLY_ACTIONS);
	
	needs_help->rw = WRITE;
	needs_help->of_status = OFPT_FLOW_MOD;
	needs_help->bytes_expected += ntohs(mod->header.length);
}



/************************************************
 * DESIGN IMPLEMENTATION:
 * 	for each packet that comes in, add a rule that
 * 	forwards per ip address to the correct port. 
 * 	This is learned incrementally
 ************************************************/
void write_new_flow(struct of_switch *learning, uint8_t table, uint32_t out_port){
	//fprintf(stderr, "Expecting %d bytes\n", learning->bytes_expected);
	struct ofp_flow_mod *mod = (struct ofp_flow_mod *)&learning->write_buffer[learning->bytes_expected];
	struct ofp_packet_in *in = (struct ofp_packet_in *)learning->read_buffer;

	/* get the ethernet address of the incoming packet (src) */
	struct ether_addr eth;
	uint8_t *in_data = (uint8_t *)&learning->read_buffer[sizeof(struct ofp_packet_in) + round_up(ntohs(in->match.length))];
	int j = 0;
	for(; j < 6; j++){
		eth.ether_addr_octet[j] = in_data[j];
	}
	
	mod->header.type    = OFPT_FLOW_MOD;
	mod->header.version = OFP_VERSION;
	mod->header.xid     = htonl(learning->xid++);

	mod->table_id     = table; 
	mod->cookie       = 0;
	mod->cookie_mask  = 0;
	mod->command      = OFPFC_ADD;
	mod->idle_timeout = htons(60); // time out in a min
	mod->hard_timeout = htons(60); // time out in a min
	mod->priority     = htons(1);  // just above default miss

	if(table == DST_TABLE){
		mod->out_port = htonl(OFPP_FLOOD);
		//now add all the oxm fields for dst (should be matching on ether addr)
		uint32_t *oxm_fields = (uint32_t *)mod->match.oxm_fields;	
		/* match on ethernet destination */
		oxm_fields[0] = htonl(OXM_OF_ETH_DST);
		uint8_t *oxm_ether_addr	= (uint8_t *)&oxm_fields[1];
		int i = 0;
		for(; i < 6; i++){
			oxm_ether_addr[i] = eth.ether_addr_octet[i];
		}
		
		mod->match.length = htons(14); //2 for type, 2 for length, 10 for dst
		
		/* now add on the instruction */
		struct ofp_instruction_actions *instr = (struct ofp_instruction_actions *)&learning->write_buffer[learning->bytes_expected + 
														  sizeof(struct ofp_flow_mod) + 
														  round_up(ntohs(mod->match.length)) - 
														  sizeof(struct ofp_match)];
		instr->type = htons(OFPIT_APPLY_ACTIONS);
		instr->len  = htons(sizeof(struct ofp_instruction_actions) + sizeof(struct ofp_action_output));
		
		//now specify the action (should be to output to this port)
		struct ofp_action_output *action = (struct ofp_action_output *)instr->actions;
		action->len  = htons(sizeof(struct ofp_action_output));
		action->type = htons(OFPAT_OUTPUT);
		action->port = htonl(out_port);
		action->max_len = htons(6400);	
		
		mod->header.length  = htons(sizeof(struct ofp_flow_mod) + sizeof(struct ofp_instruction_actions) + 
					    sizeof(struct ofp_action_output) + round_up(ntohs(mod->match.length)) - 
					    sizeof(struct ofp_match));	
		mod->buffer_id    = htonl(OFP_NO_BUFFER); // no buffer for this rule
	}
	else{
		//now add all the oxm fields for src (should be matching on port and ether_addr)
		uint32_t *oxm_port = (uint32_t *)mod->match.oxm_fields;	
		oxm_port[0] = htonl(OXM_OF_IN_PORT); 	//match on input port
		oxm_port[1] = htonl(out_port);		//add in port
		oxm_port[2] = htonl(OXM_OF_ETH_SRC);
		uint8_t *oxm_ether_addr	= (uint8_t *)&oxm_port[3];
		int i = 0;
		for(; i < 6; i++){
			oxm_ether_addr[i] = eth.ether_addr_octet[i];
		}
		
		mod->match.length = htons(22); //2 for type, 2 for length, 8 for port, 10 for either dst or src
		
		mod->out_port = htonl(OFPP_FLOOD); //clarify that this is right
		
		//we have the src table and we want to instruct to go to the dst table after
		struct ofp_instruction_goto_table *to_table = (struct ofp_instruction_goto_table *)&learning->write_buffer[sizeof(struct ofp_flow_mod) + 
															   round_up(ntohs(mod->match.length)) - 
															   sizeof(struct ofp_match)];
		to_table->type     = htons(OFPIT_GOTO_TABLE);
		to_table->len      = htons(sizeof(struct ofp_instruction_goto_table));
		to_table->table_id = DST_TABLE;
	
		mod->header.length  = htons(sizeof(struct ofp_flow_mod) + sizeof(struct ofp_instruction_goto_table) + 
					    round_up(ntohs(mod->match.length) - sizeof(struct ofp_match)));	
		mod->buffer_id    = in->buffer_id; //apply to buffered packet that triggered new mod
	}
	mod->out_group    = 0; //shouldn't matter for basic implementation
	mod->flags        = htons(OFPFF_SEND_FLOW_REM | OFPFF_CHECK_OVERLAP | OFPFF_RESET_COUNTS); //check overlap should be the only one necessary
	mod->match.type   = htons(OFPMT_OXM);

	//set top level header last because of different length for source and dest packets	
	learning->rw = WRITE;
	learning->of_status = OFPT_FLOW_MOD;
	learning->bytes_expected += ntohs(mod->header.length);
}

/* ip match should be the table id -> 0 is src, 1 is dst */ 
void write_flow_mod(struct of_switch *mod_sw, int reason, struct node *connection, uint8_t ip_match, uint32_t out_port){
	//plan on making a learning switch that matches on the first 
	if(reason == DEFAULT_FLOW){
		write_default_miss(mod_sw, 0);
		write_default_miss(mod_sw, 1);
	}
	else if(reason == NEW_FLOW){
		write_new_flow(mod_sw, ip_match, out_port);	
	}
	else if(reason == UNKNOWN){
		fprintf(stderr, "NOT A CHANCE MAN!\n");
		exit(1);
	}
	else{
		fprintf(stderr, "If you got here, something went REALLY wrong.\n");
		exit(1);
	}
}

/* returns the fd of the sending switch if switch connection,
 * otherwise returns 0
 */
int is_switch_connection(uint8_t *data){
	struct probe_packet *probe  = (struct probe_packet *)data;
	//fprintf(stderr, "Type of packet: %d\n", ntohs(probe->e.type));
	
	if(ntohs(probe->e.type) < 100){
		uint16_t switch_fd = ntohs(probe->e.type);
		//fprintf(stderr, "Type of incoming packet: %d\n", switch_fd);		
		//fprintf(stderr, "switch to switch connection!\n");
		return switch_fd;	
	}
	return 0;
}

struct node *check_network(struct of_switch *s){
		
	return (struct node *)NULL;
}

uint32_t get_input_port(struct ofp_match *match){
	uint32_t port = 0;
	uint8_t i = 0;
	while(i < ntohs(match->length)){
		if(OXM_TYPE(match->oxm_fields[i]) == OFPXMT_OFB_IN_PORT){
			i += 4; //offset 2 for field and length bytes
			//fprintf(stderr, "FOUND THE OXM PORT FIELD! NOICE\n");
			port = ntohl(match->oxm_fields[i] | (match->oxm_fields[i + 1] << 8) |
				     (match->oxm_fields[i + 2] << 16) | (match->oxm_fields[i + 3] << 24)); //do some shifting magic
			break;
		}
		else{
			i += OXM_LENGTH(match->oxm_fields[i]); //get the length and add that to i to get to next field
		}
	}

	//fprintf(stderr, "Port is: %d\n", port);
	return port;
}


void print_mac_address(uint8_t *mac){
	fprintf(stderr, "Mac address: %02x:%02x:%02x:%02x:%02x:%02x\n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* when a loop is detected, send a port_mod to not forward traffic
 * through the specified port 
 *
 * When this loop is removed, or we need the link back, we re-enable the
 * port
 * */
void change_port_behavior(struct of_switch *changing_switch, uint32_t port, uint8_t enable){
	fprintf(stderr, "Changing switch %d, port %d!\n", changing_switch->socket_fd, port);
	struct ofp_port_mod *port_mod = (struct ofp_port_mod *)changing_switch->write_buffer;
	
	port_mod->header.type    = OFPT_PORT_MOD;
	port_mod->header.version = OFP_VERSION;
	port_mod->header.xid     = htonl(changing_switch->xid++);
	port_mod->header.length  = htons(sizeof(struct ofp_port_mod));

	port_mod->port_no = htonl(port);

	memcpy(&port_mod->hw_addr[0], &changing_switch->connected_ports[port].hw_addr[0], OFP_ETH_ALEN);
	
	if(enable){
		port_mod->config  = htonl(0); //enable fwding and receiving
		port_mod->mask  = htonl(OFPPC_NO_FWD | OFPPC_NO_RECV);   //change those two flags
	}
	else{
		port_mod->config  = htonl(OFPPC_NO_FWD | OFPPC_NO_RECV); //drop all incoming and outgoing packets
		port_mod->mask  = htonl(OFPPC_NO_FWD | OFPPC_NO_RECV);   //change those two flags
	}
	
	changing_switch->rw = WRITE;
	changing_switch->of_status = OFPT_PORT_MOD;
	changing_switch->bytes_expected = ntohs(port_mod->header.length);
}



void read_packet_in(struct of_switch *r_switch){
	struct ofp_packet_in *pkt = (struct ofp_packet_in *)r_switch->read_buffer;
	//fprintf(stderr, "\n================="
	//		"\n=== Packet in ==="
	//		"\n=================\n"
	//		"  Switch   : %d\n"
	//		"  Buffer ID: 0x%08x\n"
	//		"  Total Len: %d\n"
	//		"  Reason   : %d\n"
	//		"  Table ID : %d"
	//		"\n=================\n\n",
	//		r_switch->socket_fd, ntohl(pkt->buffer_id), ntohs(pkt->total_len),
	//		pkt->reason, pkt->table_id);
	/* get the port that the packet came in on to the switch */
	struct ofp_match *match = &pkt->match;
	//search for the input port
	uint32_t input_port = get_input_port(match);

	/******************************************************
	 * check to see if we know what to do with the packet
	 * if we do: write a flow mod
	 * if not, flood the packet out of the rest of the ports
	 *******************************************************/
	if(pkt->reason == OFPR_NO_MATCH){
		struct node *connection = NULL;
		int nextdoor_fd = 0; //0 is not a valid fd, so this is safe
		if((nextdoor_fd = is_switch_connection((uint8_t *)&r_switch->read_buffer[ntohs(pkt->header.length) - ntohs(pkt->total_len)]))){
			//add a connection between two switches. If there is a loop, make a port mod
			add_connection(r_switch->socket_fd, nextdoor_fd, 0, input_port);
		}
		else{
			/* add flow on both input and output tables */
			write_flow_mod(r_switch, NEW_FLOW, connection, SRC_TABLE, input_port);
			write_flow_mod(r_switch, NEW_FLOW, connection, DST_TABLE, input_port);
		}
	}
}

/* length is the length of the ENTIRE openflow packet */
void print_port_stats(struct ofp_port_stats *ports, uint16_t packet_length){
	packet_length -= sizeof(struct ofp_multipart_reply); //now the length should be just ports
	int num_ports = packet_length / sizeof(struct ofp_port_stats); // get num of ports included
	fprintf(stderr, "<><><><><><><><><><><><><><><><>\n"
			"<><><><><> PORT STATS <><><><><>\n"
			"<><><><><><><><><><><><><><><><>\n\n");
	
	int i = 0;
	for(i = 0; i < num_ports; i++){
		fprintf(stderr, "<><>\tPort Number: %08u<><>\n"
				"<><>\tDuration   : %08u<><>\n"
				"<><>\tRx Packets : %08lu<><>\n"
				"<><>\tTx Packets : %08lu<><>\n"
				"<><>\tRx Dropped : %08lu<><>\n"
				"<><>\tTx Dropped : %08lu<><>\n\n\n",
		ports[i].port_no, ports[i].duration_sec, ports[i].rx_packets, 
		ports[i].tx_packets, ports[i].rx_dropped, ports[i].tx_dropped);
	}
	fprintf(stderr, "<><><><><><><><><><><><><><><><>\n\n");
}

void save_connected_ports(struct of_switch *s){
	struct ofp_multipart_reply *m = (struct ofp_multipart_reply *)s->read_buffer;
	//move to the part with ports
	int num_ports = (ntohs(m->header.length) - sizeof(struct ofp_multipart_reply)) / sizeof(struct ofp_port); // get num of ports included
	struct ofp_port *port = (struct ofp_port *)(m->body);
	int i = 0;
	//fprintf(stderr, "Switch %d\n", s->socket_fd);
	for(i = 0; i < num_ports; i++){
		//get the port number and dont save local port
		if(ntohl(port[i].port_no) == OFPP_LOCAL){
			continue;
		}
		uint32_t j = ntohl(port[i].port_no);
		//fprintf(stderr, "\tport %d: ", j);
		//print_mac_address(&port[i].hw_addr[0]);
		//we have to check this in case the ports are given to us out of order (rare)
		memcpy(&s->connected_ports[j], &port[i], sizeof(struct ofp_port));
	}
}

void handle_multipart_reply(struct of_switch *loaded_switch){
	struct ofp_multipart_reply *multi = (struct ofp_multipart_reply *)loaded_switch->read_buffer;
		
	switch(ntohs(multi->type)){
		case OFPMP_PORT_STATS :
			fprintf(stderr, "Multipart message concerning ports!\n");
			print_port_stats((struct ofp_port_stats *)&multi->body[0], ntohs(multi->header.length));
			break;

		case OFPMP_PORT_DESC :
			//fprintf(stderr, "port decriptions to follow:\n");
			save_connected_ports(loaded_switch);
			break;
		default :
			//fprintf(stderr, "Multipart Message not supported yet!\n");
			break;
	
	}
}
