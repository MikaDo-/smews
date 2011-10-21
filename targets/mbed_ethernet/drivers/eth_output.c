/*
* Copyright or © or Copr. 2011, Michael Hauspie
* 
* Author e-mail: michael.hauspie@lifl.fr
* 
* This software is a computer program whose purpose is to design an
* efficient Web server for very-constrained embedded system.
* 
* This software is governed by the CeCILL license under French law and
* abiding by the rules of distribution of free software.  You can  use, 
* modify and/ or redistribute the software under the terms of the CeCILL
* license as circulated by CEA, CNRS and INRIA at the following URL
* "http://www.cecill.info". 
* 
* As a counterpart to the access to the source code and  rights to copy,
* modify and redistribute granted by the license, users are provided only
* with a limited warranty  and the software's author,  the holder of the
* economic rights,  and the successive licensors  have only  limited
* liability. 
* 
* In this respect, the user's attention is drawn to the risks associated
* with loading,  using,  modifying and/or developing or reproducing the
* software by the user in light of its specific status of free software,
* that may mean  that it is complicated to manipulate,  and  that  also
* therefore means  that it is reserved for developers  and  experienced
* professionals having in-depth computer knowledge. Users are therefore
* encouraged to load and test the software's suitability as regards their
* requirements in conditions enabling the security of their systems and/or 
* data to be ensured and,  more generally, to use and operate it in the 
* same conditions as regards security. 
* 
* The fact that you are presently reading this means that you have had
* knowledge of the CeCILL license and that you accept its terms.
*/
/*
  Author: Michael Hauspie <michael.hauspie@univ-lille1.fr>
  Created: 2011-09-02
  Time-stamp: <2011-09-07 10:08:57 (mickey)>
*/
#include <stdint.h>
#include <string.h> /* memcpy */

#include <rflpc17xx/drivers/ethernet.h>

#include "hardware.h"
#include "target.h"
#include "mbed_debug.h"
#include "protocols.h"
#include "arp_cache.h"

uint8_t *current_tx_frame = NULL;
uint32_t current_tx_frame_idx = 0;

void mbed_eth_prepare_output(uint32_t size)
{
    rfEthDescriptor *d;
    rfEthTxStatus *s;
    if (current_tx_frame != NULL)
    {
	MBED_DEBUG("Asking to send a new packet while previous not finished\r\n");
	return;
    }
    if (size + PROTO_MAC_HLEN > TX_BUFFER_SIZE)
    {
	MBED_DEBUG("Trying to send a %d bytes packet. Dropping\r\n", size);
	return;
    }
    if (!rflpc_eth_get_current_tx_packet_descriptor(&d, &s))
    {
	MBED_DEBUG("No more output descriptor available\r\n");
	return;
    }
    current_tx_frame = d->packet;
    current_tx_frame_idx = PROTO_MAC_HLEN; /* put the idx at the first IP byte */
}

void mbed_eth_put_byte(uint8_t byte)
{
    if (current_tx_frame == NULL)
    {
	MBED_DEBUG("Trying to add byte %02x (%c) while prepare_output has not been successfully called\r\n", byte, byte);
	return;
    }
    if (current_tx_frame_idx >= TX_BUFFER_SIZE)
    {
	MBED_DEBUG("Trying to add byte %02x (%c) and output buffer is full\r\n", byte, byte);
	return;
    }
/*    MBED_DEBUG("O: %02x (%c)\r\n", byte, byte);*/
    current_tx_frame[current_tx_frame_idx++] = byte;
}

void mbed_eth_put_nbytes(uint8_t *bytes, uint32_t n)
{
    if (current_tx_frame == NULL)
    {
	MBED_DEBUG("Trying to add %d bytes while prepare_output has not been successfully called\r\n", n);
	return;
    }
    if (current_tx_frame_idx + n >= TX_BUFFER_SIZE)
    {
	MBED_DEBUG("Trying to add %d bytes and output buffer is full\r\n", n);
	return;
    }
    memcpy(current_tx_frame + current_tx_frame_idx, bytes, n);
    current_tx_frame_idx += n;
}

void mbed_eth_output_done()
{
    EthHead eth;
    rfEthDescriptor *d;
    rfEthTxStatus *s;
    uint32_t ip;
    if (current_tx_frame == NULL)
    {
	MBED_DEBUG("Trying to send packet before preparing it\r\n");
	return;
    }
    if (!rflpc_eth_get_current_tx_packet_descriptor(&d, &s))
    {
	MBED_DEBUG("Failed to get current output descriptor, dropping\r\n");
	current_tx_frame = NULL;
	current_tx_frame_idx = 0;
	return;
    }
    eth.src = local_eth_addr;
    ip = proto_ip_get_dst(current_tx_frame + PROTO_MAC_HLEN);
    if (!arp_get_mac(ip, &eth.dst))
    {
	MBED_DEBUG("No MAC address known for %d.%d.%d.%d, dropping\r\n", 
		   ip & 0xFF, 
		   (ip >> 8) & 0xFF,
		   (ip >> 16) & 0xFF,
		   (ip >> 24) & 0xFF
	    );
	current_tx_frame = NULL;
	current_tx_frame_idx = 0;
	return;
    }
    eth.type = PROTO_IP;
    proto_eth_mangle(&eth, current_tx_frame);
    rflpc_eth_set_tx_control_word(current_tx_frame_idx, &d->control); /* send control word (size + send options) */
    rflpc_eth_done_process_tx_packet(); /* send packet */
    current_tx_frame = NULL;
    current_tx_frame_idx = 0;
    MBED_DEBUG("Done sending packet\r\n");
}
