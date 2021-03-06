/*
 * network.h
 *
 *  Created on: Aug 1, 2017
 *      Author: wizath
 */

#ifndef NETWORK_H_
#define NETWORK_H_

#include "wizchip_conf.h"

#define DHCP_SOCKET				7
#define DHCP_MAX_RETRIES		32

#define STM_GetUniqueID(x)		((x >= 0 && x < 12) ? (*(uint8_t *) (0x1FFF7A10 + (x))) : 0)

// wiznet library functions
void wizchip_select(void);
void wizchip_deselect(void);
void wizchip_write(uint8_t wb);
uint8_t wizchip_read();

// network configuration
void net_init(void);
void net_conf(wiz_NetInfo * netinfo);
void set_net_conf(uint8_t * ipsrc, uint8_t * ipdst, uint8_t * subnet);
void display_net_conf(void);
void set_mac_address(uint8_t * macaddress);
void set_default_mac_address(void);
void load_network_values(wiz_NetInfo *glWIZNETINFO);

// server task restart
void prvRestartServerTask(void);

// dhcp related
void ldhcp_ip_assign(void);
void ldhcp_ip_conflict(void);

// dhcp task control
void prvDHCPTask(void *pvParameters);
void prvDHCPTaskStart(void);
void prvDHCPTaskStop(void);
void prvDHCPTaskRestart(void);

uint8_t prvCheckValidIPAddress(char * ipstr, uint8_t * result);
uint8_t prvCheckValidMACAddress(char * ipstr, uint8_t * result);

#endif /* NETWORK_H_ */
