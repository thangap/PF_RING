/*
 *
 * (C) 2005-11 - Luca Deri <deri@ntop.org>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lessed General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 *
 * This code includes contributions courtesy of
 * - Fedor Sakharov <fedor.sakharov@gmail.com>
 *
 */

#define __USE_XOPEN2K
#include <sys/types.h>
#include <pthread.h>

#include "pfring.h"
#include "pfring_mod.h"
#include "pfring_utils.h"
#include "pfring_mod_dna.h"

//#define RING_DEBUG

/* ******************************* */

static int pfring_map_dna_device(pfring *ring,
				 dna_device_operation operation,
				 char *device_name,
				 int32_t channel_id) {
  dna_device_mapping mapping;

  if(ring->last_dna_operation == operation) {
    fprintf(stderr, "%s(): operation (%s) already performed\n",
	    __FUNCTION__, operation == remove_device_mapping ?
	    "remove_device_mapping":"add_device_mapping");
    return (-1);
  } else
    ring->last_dna_operation = operation;

  mapping.operation = operation;
  snprintf(mapping.device_name, sizeof(mapping.device_name),
	   "%s", device_name);
  mapping.channel_id = channel_id;

  return(ring ? setsockopt(ring->fd, 0, SO_MAP_DNA_DEVICE,
			   &mapping, sizeof(mapping)): -1);
}

/* **************************************************** */

void pfring_dna_close(pfring *ring) {
  dna_term(ring);

  if(ring->dna_dev.packet_memory != 0)
    munmap((void*)ring->dna_dev.packet_memory,
	     ring->dna_dev.packet_memory_tot_len);

  if(ring->dna_dev.descr_packet_memory != NULL)
    munmap(ring->dna_dev.descr_packet_memory,
           ring->dna_dev.descr_packet_memory_tot_len);

  if(ring->dna_dev.phys_card_memory != NULL)
    munmap(ring->dna_dev.phys_card_memory,
           ring->dna_dev.phys_card_memory_len);

  pfring_map_dna_device(ring, remove_device_mapping,
                        "", ring->dna_dev.channel_id);

  if(ring->clear_promisc)
    set_if_promisc(ring->device_name, 0);

  close(ring->fd);
}

/* **************************************************** */

int pfring_dna_stats(pfring *ring, pfring_stat *stats) {
  stats->recv = ring->tot_dna_read_pkts, stats->drop = 0;
  return(0);
}

/* **************************************************** */

void pfring_dna_recv_multiple(pfring *ring,
			      pfringProcesssPacket looper,
			      struct pfring_pkthdr *hdr,
			      char *buffer, u_int buffer_len,
			      u_int8_t wait_for_packet,
			      void *user_data) {
  if(ring->reentrant) pthread_spin_lock(&ring->spinlock);

  if(buffer == NULL) buffer_len = 0;

  ring->break_recv_loop = 0;

  while(!ring->break_recv_loop) {
    u_char *pkt;

    if(ring->is_shutting_down) break;

    pkt = (u_char*)dna_get_next_packet(ring, buffer, buffer_len, hdr);

    if(pkt) {
      if(buffer) {
	// gettimeofday(&hdr->ts, NULL);
	parse_pkt((char*)pkt, hdr);
      }
      looper(hdr, pkt, user_data);
    } else {
      if(wait_for_packet) {
	dna_there_is_a_packet_to_read(ring, 1);
	// usleep(1); /* Can be removed */
      }
    }
  }

  if(ring->reentrant) pthread_spin_unlock(&ring->spinlock);
}

/* **************************************************** */

int pfring_dna_recv(pfring *ring, char* buffer, u_int buffer_len,
		struct pfring_pkthdr *hdr,
		u_int8_t wait_for_incoming_packet) {
  char *pkt = NULL;
  int8_t status = 1;

  if(ring->is_shutting_down) return(-1);

  ring->break_recv_loop = 0;
  if(ring->reentrant) pthread_spin_lock(&ring->spinlock);

  redo_pfring_recv:
    if(ring->is_shutting_down || ring->break_recv_loop) {
      if(ring->reentrant) pthread_spin_unlock(&ring->spinlock);
      return(-1);
    }

    pkt = dna_get_next_packet(ring, buffer, buffer_len, hdr);

    if(pkt && (hdr->len > 0)) {
      /* Set the (1) below to (0) for enabling packet parsing for DNA devices */
      if(0)
	hdr->extended_hdr.parsed_header_len = 0;
      else if(buffer)
	parse_pkt(buffer, hdr);

      if(ring->reentrant) pthread_spin_unlock(&ring->spinlock);
      return(1);
    }

    if(wait_for_incoming_packet) {
      status = dna_there_is_a_packet_to_read(ring, wait_for_incoming_packet);
    }

    if(status > 0)
      goto redo_pfring_recv;

    if(ring->reentrant) pthread_spin_unlock(&ring->spinlock);
    return(0);
 }

/* ******************************* */

static int pfring_get_mapped_dna_device(pfring *ring, dna_device *dev) {
  socklen_t len = sizeof(dna_device);

  if(dev == NULL)
    return(-1);
  else
    return(getsockopt(ring->fd, 0, SO_GET_MAPPED_DNA_DEVICE, dev, &len));
}

/* **************************************************** */

#ifdef DEBUG

static void pfring_dump_dna_stats(pfring* ring) {
  dna_dump_stats(ring);
}

#endif

/* **************************************************** */

int pfring_dna_open(pfring *ring) {
  int   channel_id = 0;
  int   rc;
  char *at;

  ring->close = pfring_dna_close;
  ring->stats = pfring_dna_stats;
  ring->recv = pfring_dna_recv;
  ring->recv_multiple = pfring_dna_recv_multiple;
  ring->get_num_rx_channels = pfring_main_get_num_rx_channels;
  ring->set_poll_duration = pfring_dna_set_poll_duration;
  ring->send = pfring_dna_send;
  ring->last_dna_operation = remove_device_mapping;
  ring->set_poll_watermark = pfring_main_set_poll_watermark;
  ring->fd = socket(PF_RING, SOCK_RAW, htons(ETH_P_ALL));

#ifdef DEBUG
  printf("Open RING [fd=%d]\n", ring->fd);
#endif

  if(ring->fd < 0)
    return -1;

  at = strchr(ring->device_name, '@');
  if(at != NULL) {
    at[0] = '\0';

    /* Syntax
       ethX@1      channel 1
     */

    channel_id = atoi(&at[1]);
  }
  /* printf("channel_id=%d\n", channel_id); */
  rc = pfring_map_dna_device(ring, add_device_mapping,
			     ring->device_name, channel_id);

  if(rc < 0) {
    printf("pfring_map_dna_device() failed [rc=%d]: device already in use, channel not existing or non-DNA driver?\n", rc);
    printf("Make sure that you load the DNA-driver *after* you loaded the PF_RING kernel module\n");
    close(ring->fd);
    return -1;
  }

  rc = pfring_get_mapped_dna_device(ring, &ring->dna_dev);

  if (rc < 0) {
      printf("pfring_get_mapped_dna_device() failed [rc=%d]\n", rc);
      pfring_map_dna_device(ring, remove_device_mapping,
			  ring->device_name, channel_id);
      close(ring->fd);
      return -1;
  }

#ifdef DEBUG
  printf("[num_slots=%d][slot_len=%d][tot_mem_len=%d]\n",
	 ring->dna_dev.packet_memory_num_slots,
	 ring->dna_dev.packet_memory_slot_len,
	 ring->dna_dev.packet_memory_tot_len);
  printf("[memory_num_slots=%d][memory_slot_len=%d]"
	 "[memory_tot_len=%d]\n",
	 ring->dna_dev.descr_packet_memory_num_slots,
	 ring->dna_dev.descr_packet_memory_slot_len,
	 ring->dna_dev.descr_packet_memory_tot_len);
#endif

  ring->dna_mapped_device = 1;

  ring->dna_dev.packet_memory =
	(unsigned long)mmap(NULL, ring->dna_dev.packet_memory_tot_len,
			    PROT_READ|PROT_WRITE,
			    MAP_SHARED, ring->fd, 0);

  if(ring->dna_dev.packet_memory == (unsigned long)MAP_FAILED) {
    printf("mmap(1) failed");
    close(ring->fd);
    return -1;
  }

  ring->dna_dev.descr_packet_memory =
	(void*)mmap(NULL, ring->dna_dev.descr_packet_memory_tot_len,
		    PROT_READ|PROT_WRITE,
		    MAP_SHARED, ring->fd, 0);

  if(ring->dna_dev.descr_packet_memory == MAP_FAILED) {
    printf("mmap(2) failed");
    close(ring->fd);
    return -1;
  }

  if(ring->dna_dev.phys_card_memory_len > 0){
    /* some DNA drivers do not use this memory */
    ring->dna_dev.phys_card_memory =
	  (void*)mmap(NULL, ring->dna_dev.phys_card_memory_len,
		      PROT_READ|PROT_WRITE,
		      MAP_SHARED, ring->fd, 0);

    if(ring->dna_dev.phys_card_memory == MAP_FAILED) {
      printf("mmap(3) failed");
      close(ring->fd);
      return -1;
    }
  }

  if(dna_init(ring, sizeof(pfring)) == -1) {
    printf("dna_init() failed\n");
    close(ring->fd);
    return -1;
  }

  if(ring->promisc) {
    if(set_if_promisc(ring->device_name, 1) == 0)
      ring->clear_promisc = 1;
  }

#ifdef DEBUG
  pfring_dump_dna_stats(ring);
#endif

  return 0;
}

/* *********************************** */

int pfring_dna_set_poll_duration(pfring *ring, u_int duration) {
  return pfring_main_set_poll_duration(ring, duration);
}

/* *********************************** */

int pfring_dna_send(pfring *ring, char *pkt, u_int pkt_len) {
  return(dna_send_packet(ring, pkt, pkt_len));
}
