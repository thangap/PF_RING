/*
 *
 * (C) 2005-11 - Luca Deri <deri@ntop.org>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesses General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef _PFRING_DNA_H_
#define _PFRING_DNA_H_

int  pfring_dna_open (pfring *ring);

void pfring_dna_close(pfring *ring);
int  pfring_dna_stats(pfring *ring, pfring_stat *stats);
int  pfring_dna_recv (pfring *ring, u_char** buffer, u_int buffer_len, 
		      struct pfring_pkthdr *hdr, u_int8_t wait_for_incoming_packet);
int  pfring_dna_send(pfring *ring, char *pkt, u_int pkt_len);
int  pfring_dna_enable_ring(pfring *ring);

/* DNA */
int dna_init(pfring* ring, u_int len);

#ifdef HAVE_NITRO
int dna_bounce_init(pfring_bounce *bounce);
int dna_bounce_loop(pfring_bounce *bounce, pfringBounceProcesssPacket looper, const u_char *user_bytes, u_int8_t wait_for_packet);
void dna_bounce_destroy(pfring_bounce *bounce);
#endif

#endif /* _PFRING_DNA_H_ */
