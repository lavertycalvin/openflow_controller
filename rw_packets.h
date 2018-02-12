#ifndef RW_PACKET_H
#define RW_PACKET_H

#include "controller.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#define RESIZE_READ_BUFFER 1
#define RESIZE_WRITE_BUFFER 2

/* function defs */
void write_openflow_hello(struct of_switch *listening_switch);	
void read_openflow_hello(struct of_switch *hi_switch);
void read_from(struct of_switch *r_switch);
void write_to(struct of_switch *w_switch);

void read_error(struct of_switch *e_switch);
void write_error(struct of_switch *e_switch);

void write_echo_request(struct of_switch *echo_switch);

void read_echo_request(struct of_switch *echo_switch);

void write_echo_reply(struct of_switch *echo_switch);
void read_echo_reply(struct of_switch *echo_switch);

void read_features(struct of_switch *feat_switch);
void request_features(struct of_switch *feat_switch);

void set_config(struct of_switch *lost_switch);
void read_config(struct of_switch *set_switch);

void read_packet_in(struct of_switch *r_switch);

void handle_multipart_reply(struct of_switch *loaded_switch);

void get_port_info(struct of_switch *unk_switch);
#endif
