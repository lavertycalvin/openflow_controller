#ifndef RW_PACKET_H
#define RW_PACKET_H

#include "controller.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#define RESIZE_READ_BUFFER 1
#define RESIZE_WRITE_BUFFER 2

#define SRC_TABLE 0
#define DST_TABLE 1

/* reasons to write a flow mod */
enum{
	DEFAULT_FLOW,
	NEW_FLOW,
	UNKNOWN
};

#define ARP  0x0806 
#define IPV4 0x0800
#define IPV6 0x86dd



struct enet_header {
	uint8_t dest[6];
	uint8_t source[6];
	uint16_t type; /*ARP, IP, Unknown*/
}__attribute__((packed));


struct probe_packet{
	struct enet_header e;
}__attribute__((packed));

/* function defs */
void write_openflow_hello(struct of_switch *listening_switch);	
void read_openflow_hello(struct of_switch *hi_switch);
void read_from(struct of_switch *r_switch);
void write_to(struct of_switch *w_switch);

void read_error(struct of_switch *e_switch);
void write_error(struct of_switch *e_switch);

void write_echo_request(struct of_switch *echo_switch);

void read_echo_request(struct of_switch *echo_switch);

void make_echo_reply(struct of_switch *echo_switch);
void read_echo_reply(struct of_switch *echo_switch);

void read_features(struct of_switch *feat_switch);
void request_features(struct of_switch *feat_switch);

void set_config(struct of_switch *lost_switch);
void read_config(struct of_switch *set_switch);

void read_packet_in(struct of_switch *r_switch);

void handle_multipart_reply(struct of_switch *loaded_switch);

void get_port_info(struct of_switch *unk_switch);

void read_port_change(struct of_switch *uneasy_switch, struct network *graph);

void write_flow_mod(struct of_switch *mod_sw, int reason, struct node *connection, uint8_t ip_match, uint32_t out_port);
void send_probe_packet(struct of_switch *new_s);


void change_port_behavior(struct of_switch *changing_switch, uint32_t port, uint8_t enable);
#endif
