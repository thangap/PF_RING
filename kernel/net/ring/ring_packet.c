/* ***************************************************************
 *
 * (C) 2004-07 - Luca Deri <deri@ntop.org>
 *
 * This code includes contributions courtesy of
 * - Jeff Randall <jrandall@nexvu.com>
 * - Helmut Manck <helmut.manck@secunet.com>
 * - Brad Doctor <brad@stillsecure.com>
 * - Amit D. Chaudhary <amit_ml@rajgad.com>
 * - Francesco Fusco <fusco@ntop.org> (IP defrag implementation)
 * - Michael Stiller <ms@2scale.net> (author of the VM memory support)
 * - Hitoshi Irino <irino@sfc.wide.ad.jp>
 * - Andrew Gallatin <gallatyn@myri.com>
 * - Matthew J. Roth <mroth@imminc.com>
 * - Vincent Carrier <vicarrier@wanadoo.fr>
 * - Marketakis Yannis <marketak@ics.forth.gr>
 * - Noam Dev <noamdev@gmail.com>
 * - Siva Kollipara <siva@cs.arizona.edu>
 * - Kevin Wormington <kworm@sofnet.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
#include <linux/autoconf.h>
#else
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <linux/init.h>
#include <linux/filter.h>
#include <linux/ring.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_TEXTSEARCH
#include <linux/textsearch.h>
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
#include <net/xfrm.h>
#else
#include <linux/poll.h>
#endif
#include <net/sock.h>
#include <asm/io.h>   /* needed for virt_to_phys() */
#ifdef CONFIG_INET
#include <net/inet_common.h>
#endif
#include <net/ip.h>

/* #define RING_DEBUG */

/* ************************************************* */

#define TH_FIN_MULTIPLIER	0x01
#define TH_SYN_MULTIPLIER	0x02
#define TH_RST_MULTIPLIER	0x04
#define TH_PUSH_MULTIPLIER	0x08
#define TH_ACK_MULTIPLIER	0x10
#define TH_URG_MULTIPLIER	0x20

/* ************************************************* */

#define CLUSTER_LEN       8

struct ring_cluster {
  u_short             cluster_id; /* 0 = no cluster */
  u_short             num_cluster_elements;
  enum cluster_type   hashing_mode;
  u_short             hashing_id;
  struct sock         *sk[CLUSTER_LEN];
};

typedef struct {
  struct ring_cluster cluster;
  struct list_head list;
} ring_cluster_element;

/* ************************************************* */

struct ring_element {
  struct list_head  list;
  struct sock      *sk;
};

/* ************************************************* */

struct ring_opt {
  u_int8_t ring_active;
  struct net_device *ring_netdev;
  u_short ring_pid;

  /* Cluster */
  u_short cluster_id; /* 0 = no cluster */

  /* Reflector */
  struct net_device *reflector_dev;

  /* Packet buffers */
  unsigned long order;

  /* Ring Slots */
  void * ring_memory;
  FlowSlotInfo *slots_info; /* Points to ring_memory */
  char *ring_slots;         /* Points to ring_memory+sizeof(FlowSlotInfo) */

  /* Packet Sampling */
  u_int32_t pktToSample, sample_rate;

  /* BPF Filter */
  struct sk_filter *bpfFilter;

  /* Filtering Rules */
  u_int16_t num_filtering_rules;
  u_int8_t rules_default_accept_policy; /* 1=default policy is accept, drop otherwise */
  struct list_head rules;

  /* Locks */
  atomic_t num_ring_slots_waiters;
  wait_queue_head_t ring_slots_waitqueue;
  rwlock_t ring_index_lock;

  /* Indexes (Internal) */
  u_int insert_page_id, insert_slot_id;
};

/* ************************************************* */

/* List of all ring sockets. */
static struct list_head ring_table;
static u_int ring_table_size;

/* List of all clusters */
struct list_head ring_cluster_list;

/* List of all plugins */
struct pfring_plugin_registration *plugin_registration[MAX_PLUGIN_ID] = { NULL };

static rwlock_t ring_mgmt_lock = RW_LOCK_UNLOCKED;

/* ********************************** */

/* /proc entry for ring module */
struct proc_dir_entry *ring_proc_dir = NULL;
struct proc_dir_entry *ring_proc = NULL;

static int ring_proc_get_info(char *, char **, off_t, int, int *, void *);
static void ring_proc_add(struct ring_opt *pfr);
static void ring_proc_remove(struct ring_opt *pfr);
static void ring_proc_init(void);
static void ring_proc_term(void);

/* ********************************** */

/* Forward */
static struct proto_ops ring_ops;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,11))
static struct proto ring_proto;
#endif

static int skb_ring_handler(struct sk_buff *skb, u_char recv_packet, u_char real_skb);
static int buffer_ring_handler(struct net_device *dev, char *data, int len);
static int remove_from_cluster(struct sock *sock, struct ring_opt *pfr);

/* Extern */
extern struct sk_buff *ip_defrag(struct sk_buff *skb, u32 user);

/* ********************************** */

/* Defaults */
static unsigned int bucket_len = 128 /* bytes */, num_slots = 4096;
static unsigned int enable_tx_capture = 1;
static unsigned int enable_ip_defrag = 0;
#if 0
static unsigned int transparent_mode = 1;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16))
module_param(bucket_len, uint, 0644);
module_param(num_slots,  uint, 0644);
#if 0
module_param(transparent_mode, uint, 0644);
#endif
module_param(enable_tx_capture, uint, 0644);
module_param(enable_ip_defrag, uint, 0644);
#else
MODULE_PARM(bucket_len, "i");
MODULE_PARM(num_slots, "i");
#if 0
MODULE_PARM(transparent_mode, "i");
#endif
MODULE_PARM(enable_tx_capture, "i");
MODULE_PARM(enable_ip_defrag, "i");
#endif

MODULE_PARM_DESC(bucket_len, "Size (in bytes) a ring bucket");
MODULE_PARM_DESC(num_slots,  "Number of ring slots");
#if 0
MODULE_PARM_DESC(transparent_mode,
                 "Set to 1 to set transparent mode "
                 "(slower but backwards compatible)");
#endif
MODULE_PARM_DESC(enable_tx_capture, "Set to 1 to capture outgoing packets");
MODULE_PARM_DESC(enable_ip_defrag,
		 "Set to 1 to enable IP defragmentation"
		 "(only rx traffic is defragmentead)");

/* ********************************** */

#define MIN_QUEUED_PKTS      64
#define MAX_QUEUE_LOOPS      64


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
#define ring_sk_datatype(__sk) ((struct ring_opt *)__sk)
#define ring_sk(__sk) ((__sk)->sk_protinfo)
#else
#define ring_sk_datatype(a) (a)
#define ring_sk(__sk) ((__sk)->protinfo.pf_ring)
#endif

#define _rdtsc() ({ uint64_t x; asm volatile("rdtsc" : "=A" (x)); x; })

/* ***************** Legacy code ************************ */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22))
static inline struct iphdr *ip_hdr(const struct sk_buff *skb)
{
  return (struct iphdr *)skb->nh.iph;
}

static inline void skb_set_network_header(struct sk_buff *skb,
					  const int offset)
{
  skb->nh.iph = (struct iphdr*)skb->data + offset;
}

static inline void skb_reset_network_header(struct sk_buff *skb)
{
  ;
}

static inline void skb_reset_transport_header(struct sk_buff *skb)
{
  ;
}
#endif

/* ***** Code taken from other kernel modules ******** */

/*
 * rvmalloc / rvfree copied from usbvideo.c
 */
static void *rvmalloc(unsigned long size)
{
  void *mem;
  unsigned long adr;
  unsigned long pages = 0;


#if defined(RING_DEBUG)
  printk("RING: rvmalloc: %lu bytes\n", size);
#endif

  size = PAGE_ALIGN(size);
  mem = vmalloc_32(size);
  if (!mem)
    return NULL;

  memset(mem, 0, size); /* Clear the ram out, no junk to the user */
  adr = (unsigned long) mem;
  while (size > 0) {
    SetPageReserved(vmalloc_to_page((void *)adr));
    pages++;
    adr += PAGE_SIZE;
    size -= PAGE_SIZE;
  }

#if defined(RING_DEBUG)
  printk("RING: rvmalloc: %lu pages\n", pages);
#endif
  return mem;
}

/* ************************************************** */

static void rvfree(void *mem, unsigned long size)
{
  unsigned long adr;
  unsigned long pages = 0;

#if defined(RING_DEBUG)
  printk("RING: rvfree: %lu bytes\n", size);
#endif

  if (!mem)
    return;

  adr = (unsigned long) mem;
  while ((long) size > 0) {
    ClearPageReserved(vmalloc_to_page((void *)adr));
    pages++;
    adr += PAGE_SIZE;
    size -= PAGE_SIZE;
  }
#if defined(RING_DEBUG)
  printk("RING: rvfree: %lu pages\n", pages);
  printk("RING: rvfree: calling vfree....\n");
#endif
  vfree(mem);
#if defined(RING_DEBUG)
  printk("RING: rvfree: after vfree....\n");
#endif
}

/* ********************************** */

#define IP_DEFRAG_RING 1234

/* Returns new sk_buff, or NULL */
static struct sk_buff *ring_gather_frags(struct sk_buff *skb)
{
  skb = ip_defrag(skb, IP_DEFRAG_RING);

  if(skb)
    ip_send_check(ip_hdr(skb));

  return(skb);
}

/* ********************************** */

static void ring_sock_destruct(struct sock *sk)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
  skb_queue_purge(&sk->sk_receive_queue);

  if (!sock_flag(sk, SOCK_DEAD)) {
#if defined(RING_DEBUG)
    printk("Attempt to release alive ring socket: %p\n", sk);
#endif
    return;
  }

  BUG_TRAP(!atomic_read(&sk->sk_rmem_alloc));
  BUG_TRAP(!atomic_read(&sk->sk_wmem_alloc));
#else
  BUG_TRAP(atomic_read(&sk->rmem_alloc)==0);
  BUG_TRAP(atomic_read(&sk->wmem_alloc)==0);

  if (!sk->dead) {
#if defined(RING_DEBUG)
    printk("Attempt to release alive ring socket: %p\n", sk);
#endif
    return;
  }
#endif

  kfree(ring_sk(sk));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
  MOD_DEC_USE_COUNT;
#endif
}

/* ********************************** */

static void ring_proc_add(struct ring_opt *pfr) {
  if(ring_proc_dir != NULL) {
    char name[16];

    pfr->ring_pid = current->pid;

    snprintf(name, sizeof(name), "%d", pfr->ring_pid);
    create_proc_read_entry(name, 0, ring_proc_dir,
			   ring_proc_get_info, pfr);
    /* printk("PF_RING: added /proc/net/pf_ring/%s\n", name); */
  }
}

/* ********************************** */

static void ring_proc_remove(struct ring_opt *pfr) {
  if(ring_proc_dir != NULL) {
    char name[16];

    snprintf(name, sizeof(name), "%d", pfr->ring_pid);
    remove_proc_entry(name, ring_proc_dir);
    /* printk("PF_RING: removed /proc/net/pf_ring/%s\n", name); */
  }
}

/* ********************************** */

static int ring_proc_get_info(char *buf, char **start, off_t offset,
			      int len, int *unused, void *data)
{
  int rlen = 0;
  struct ring_opt *pfr;
  FlowSlotInfo *fsi;

  if(data == NULL) {
    /* /proc/net/pf_ring/info */
    rlen = sprintf(buf,        "Version             : %s\n", RING_VERSION);
    rlen += sprintf(buf + rlen,"Bucket length       : %d bytes\n", bucket_len);
    rlen += sprintf(buf + rlen,"Ring slots          : %d\n", num_slots);
    rlen += sprintf(buf + rlen,"Slot version        : %d\n", RING_FLOWSLOT_VERSION);
    rlen += sprintf(buf + rlen,"Capture TX          : %s\n",
		    enable_tx_capture ? "Yes [RX+TX]" : "No [RX only]");
    rlen += sprintf(buf + rlen,"IP Defragment       : %s\n",  enable_ip_defrag ? "Yes" : "No");

#if 0
    rlen += sprintf(buf + rlen,"Transparent mode    : %s\n",
                    transparent_mode ? "Yes" : "No");
#endif
    rlen += sprintf(buf + rlen,"Total rings         : %d\n", ring_table_size);
  } else {
    /* detailed statistics about a PF_RING */
    pfr = (struct ring_opt*)data;

    if(data) {
      fsi = pfr->slots_info;

      if(fsi) {
	rlen = sprintf(buf,        "Bound Device  : %s\n",
		       pfr->ring_netdev->name == NULL ? "<NULL>" : pfr->ring_netdev->name);
	rlen += sprintf(buf + rlen, "Version       : %d\n",  fsi->version);
	rlen += sprintf(buf + rlen, "Sampling Rate : %d\n",  pfr->sample_rate);
	rlen += sprintf(buf + rlen, "IP Defragment : %s\n",  enable_ip_defrag ? "Yes" : "No");
	rlen += sprintf(buf + rlen, "BPF Filtering : %s\n",  pfr->bpfFilter ? "Enabled" : "Disabled");
	rlen += sprintf(buf + rlen, "# Filt. Rules : %d\n",  pfr->num_filtering_rules);
	rlen += sprintf(buf + rlen, "Cluster Id    : %d\n",  pfr->cluster_id);
	rlen += sprintf(buf + rlen, "Tot Slots     : %d\n",  fsi->tot_slots);
	rlen += sprintf(buf + rlen, "Slot Len      : %d\n",  fsi->slot_len);
	rlen += sprintf(buf + rlen, "Data Len      : %d\n",  fsi->data_len);
	rlen += sprintf(buf + rlen, "Tot Memory    : %d\n",  fsi->tot_mem);
	rlen += sprintf(buf + rlen, "Tot Packets   : %lu\n", (unsigned long)fsi->tot_pkts);
	rlen += sprintf(buf + rlen, "Tot Pkt Lost  : %lu\n", (unsigned long)fsi->tot_lost);
	rlen += sprintf(buf + rlen, "Tot Insert    : %lu\n", (unsigned long)fsi->tot_insert);
	rlen += sprintf(buf + rlen, "Tot Read      : %lu\n", (unsigned long)fsi->tot_read);
      } else
	rlen = sprintf(buf, "WARNING fsi == NULL\n");
    } else
      rlen = sprintf(buf, "WARNING data == NULL\n");
  }

  return rlen;
}

/* ********************************** */

static void ring_proc_init(void) {
  ring_proc_dir = proc_mkdir("pf_ring", proc_net);

  if(ring_proc_dir) {
    ring_proc_dir->owner = THIS_MODULE;
    ring_proc = create_proc_read_entry("info", 0, ring_proc_dir,
				       ring_proc_get_info, NULL);
    if(!ring_proc)
      printk("PF_RING: unable to register proc file\n");
    else {
      ring_proc->owner = THIS_MODULE;
      printk("PF_RING: registered /proc/net/pf_ring/\n");
    }
  } else
    printk("PF_RING: unable to create /proc/net/pf_ring\n");
}

/* ********************************** */

static void ring_proc_term(void) {
  if(ring_proc != NULL) {
    remove_proc_entry("info", ring_proc_dir);
    if(ring_proc_dir != NULL) remove_proc_entry("pf_ring", proc_net);

    printk("PF_RING: deregistered /proc/net/pf_ring\n");
  }
}

/* ********************************** */

/*
 * ring_insert()
 *
 * store the sk in a new element and add it
 * to the head of the list.
 */
static inline void ring_insert(struct sock *sk) {
  struct ring_element *next;

#if defined(RING_DEBUG)
  printk("RING: ring_insert()\n");
#endif

  next = kmalloc(sizeof(struct ring_element), GFP_ATOMIC);
  if(next != NULL) {
    next->sk = sk;
    write_lock_irq(&ring_mgmt_lock);
    list_add(&next->list, &ring_table);
    write_unlock_irq(&ring_mgmt_lock);
  } else {
    if(net_ratelimit())
      printk("RING: could not kmalloc slot!!\n");
  }

  ring_table_size++;
  ring_proc_add(ring_sk(sk));
}

/* ********************************** */

/*
 * ring_remove()
 *
 * For each of the elements in the list:
 *  - check if this is the element we want to delete
 *  - if it is, remove it from the list, and free it.
 *
 * stop when we find the one we're looking for (break),
 * or when we reach the end of the list.
 */
static inline void ring_remove(struct sock *sk) {
  struct list_head *ptr, *tmp_ptr;
  struct ring_element *entry;

#if defined(RING_DEBUG)
  printk("RING: ring_remove()\n");
#endif

  list_for_each_safe(ptr, tmp_ptr, &ring_table) {
    entry = list_entry(ptr, struct ring_element, list);

    if(entry->sk == sk) {
      list_del(ptr);
      kfree(ptr);
      ring_table_size--;
      break;
    }
  }

#if defined(RING_DEBUG)
  printk("RING: leaving ring_remove()\n");
#endif
}

/* ********************************** */

static u_int32_t num_queued_pkts(struct ring_opt *pfr) {

  if(pfr->ring_slots != NULL) {
    u_int32_t tot_insert = pfr->slots_info->insert_idx,
#if defined(RING_DEBUG)
      tot_read = pfr->slots_info->tot_read, tot_pkts;
#else
    tot_read = pfr->slots_info->tot_read;
#endif

    if(tot_insert >= tot_read) {
#if defined(RING_DEBUG)
      tot_pkts = tot_insert-tot_read;
#endif
      return(tot_insert-tot_read);
    } else {
#if defined(RING_DEBUG)
      tot_pkts = ((u_int32_t)-1)+tot_insert-tot_read;
#endif
      return(((u_int32_t)-1)+tot_insert-tot_read);
    }

#if defined(RING_DEBUG)
    printk("-> num_queued_pkts=%d [tot_insert=%d][tot_read=%d]\n",
	   tot_pkts, tot_insert, tot_read);
#endif

  } else
    return(0);
}

/* ********************************** */

static inline FlowSlot* get_insert_slot(struct ring_opt *pfr) {
#if defined(RING_DEBUG)
  printk("get_insert_slot(%d)\n", pfr->slots_info->insert_idx);
#endif

  if(pfr->ring_slots != NULL) {
    FlowSlot *slot = (FlowSlot*)&(pfr->ring_slots[pfr->slots_info->insert_idx
						  *pfr->slots_info->slot_len]);
    return(slot);
  } else
    return(NULL);
}

/* ********************************** */

static inline FlowSlot* get_remove_slot(struct ring_opt *pfr) {
#if defined(RING_DEBUG)
  printk("get_remove_slot(%d)\n", pfr->slots_info->remove_idx);
#endif

  if(pfr->ring_slots != NULL)
    return((FlowSlot*)&(pfr->ring_slots[pfr->slots_info->remove_idx*
					pfr->slots_info->slot_len]));
  else
    return(NULL);
}

/* ******************************************************* */

static int parse_pkt(struct sk_buff *skb,
		     u_int16_t skb_displ,
		     struct pcap_pkthdr *hdr)
{
  struct iphdr *ip;
  struct ethhdr *eh = (struct ethhdr*)(skb->data-skb_displ);
  u_int16_t displ;

  memset(&hdr->parsed_pkt, 0, sizeof(struct pkt_parsing_info));

  hdr->parsed_pkt.eth_type = ntohs(eh->h_proto);
  hdr->parsed_pkt.eth_offset = -skb_displ;

  if(hdr->parsed_pkt.eth_type == 0x8100 /* 802.1q (VLAN) */)
    {
      hdr->parsed_pkt.vlan_offset = hdr->parsed_pkt.eth_offset + sizeof(struct ethhdr);
      hdr->parsed_pkt.vlan_id = (skb->data[14] & 15) * 256 + skb->data[15];
      hdr->parsed_pkt.eth_type = (skb->data[16]) * 256 + skb->data[17];
      displ = 4;
    }
  else
    {
      displ = 0;
      hdr->parsed_pkt.vlan_id = (u_int16_t)-1;
    }

  if(hdr->parsed_pkt.eth_type == 0x0800 /* IP */) {
    hdr->parsed_pkt.l3_offset = displ+sizeof(struct ethhdr);
    ip = (struct iphdr*)(skb->data-skb_displ+hdr->parsed_pkt.l3_offset);

    hdr->parsed_pkt.ipv4_src = ntohl(ip->saddr), hdr->parsed_pkt.ipv4_dst = ntohl(ip->daddr), hdr->parsed_pkt.l3_proto = ip->protocol;
    hdr->parsed_pkt.ipv4_tos = ip->tos;

    if((ip->protocol == IPPROTO_TCP) || (ip->protocol == IPPROTO_UDP))
      {
	hdr->parsed_pkt.l4_offset = hdr->parsed_pkt.l3_offset+ip->ihl*4;

	if(ip->protocol == IPPROTO_TCP)
	  {
	    struct tcphdr *tcp = (struct tcphdr*)(skb->data-skb_displ+hdr->parsed_pkt.l4_offset);
	    hdr->parsed_pkt.l4_src_port = ntohs(tcp->source), hdr->parsed_pkt.l4_dst_port = ntohs(tcp->dest);
	    hdr->parsed_pkt.payload_offset = hdr->parsed_pkt.l4_offset+(tcp->doff * 4);
	    hdr->parsed_pkt.tcp_flags = (tcp->fin * TH_FIN_MULTIPLIER) + (tcp->syn * TH_SYN_MULTIPLIER) + (tcp->rst * TH_RST_MULTIPLIER) +
	      (tcp->psh * TH_PUSH_MULTIPLIER) + (tcp->ack * TH_ACK_MULTIPLIER) + (tcp->urg * TH_URG_MULTIPLIER);
	  } else if(ip->protocol == IPPROTO_UDP)
	    {
	      struct udphdr *udp = (struct udphdr*)(skb->data-skb_displ+hdr->parsed_pkt.l4_offset);
	      hdr->parsed_pkt.l4_src_port = ntohs(udp->source), hdr->parsed_pkt.l4_dst_port = ntohs(udp->dest);
	      hdr->parsed_pkt.payload_offset = hdr->parsed_pkt.l4_offset+sizeof(struct udphdr);
	    } else
	      hdr->parsed_pkt.payload_offset = hdr->parsed_pkt.l4_offset;
      } else
	hdr->parsed_pkt.l4_src_port = hdr->parsed_pkt.l4_dst_port = 0;

    return(1); /* IP */
  } /* TODO: handle IPv6 */

  return(0); /* No IP */
}

/* ********************************** */

inline int MatchFound (void* id, int index, void *data) { return(0); }

/* ********************************** */

/* 0 = no match, 1 = match */
static int match_filtering_rule(filtering_rule_element *rule,
				struct pcap_pkthdr *hdr,
				struct sk_buff *skb,
				int displ)
{
  int debug = 1;

  if(debug) {
    printk("match_filtering_rule(vlan=%u, proto=%u, sip=%u, sport=%u, dip=%u, dport=%u) ",
	   hdr->parsed_pkt.vlan_id, hdr->parsed_pkt.l3_proto, hdr->parsed_pkt.ipv4_src, hdr->parsed_pkt.l4_src_port,
	   hdr->parsed_pkt.ipv4_dst, hdr->parsed_pkt.l4_dst_port);
    printk("[rule(vlan=%u, proto=%u, ip=%u-%u, port=%u-%u)]\n",
	   rule->rule.core_fields.vlan_id, rule->rule.core_fields.proto, 
	   rule->rule.core_fields.host_low, rule->rule.core_fields.host_high, 
	   rule->rule.core_fields.port_low,
	   rule->rule.core_fields.port_high);
  }

  if((rule->rule.core_fields.vlan_id > 0) && (hdr->parsed_pkt.vlan_id  != rule->rule.core_fields.vlan_id)) return(0);
  if((rule->rule.core_fields.proto > 0)   && (hdr->parsed_pkt.l3_proto != rule->rule.core_fields.proto))   return(0);

  if(rule->rule.core_fields.host_low > 0) {
    if((hdr->parsed_pkt.ipv4_src < rule->rule.core_fields.host_low) 
       || (hdr->parsed_pkt.ipv4_src > rule->rule.core_fields.host_high)
       || (hdr->parsed_pkt.ipv4_dst < rule->rule.core_fields.host_low) 
       || (hdr->parsed_pkt.ipv4_dst > rule->rule.core_fields.host_high))
      return(0);
  }

  if((rule->rule.core_fields.port_high > 0)
     && (!((hdr->parsed_pkt.l4_src_port >= rule->rule.core_fields.port_low) && (hdr->parsed_pkt.l4_src_port <= rule->rule.core_fields.port_high)))
     && (!((hdr->parsed_pkt.l4_dst_port >= rule->rule.core_fields.port_low) && (hdr->parsed_pkt.l4_dst_port <= rule->rule.core_fields.port_high))))
    return(0);

  if(rule->rule.balance_pool > 0) {
    u_int32_t balance_hash = (hdr->parsed_pkt.vlan_id
			      + hdr->parsed_pkt.l3_proto
			      + hdr->parsed_pkt.ipv4_src + hdr->parsed_pkt.l4_src_port
			      + hdr->parsed_pkt.ipv4_dst + hdr->parsed_pkt.l4_dst_port) % rule->rule.balance_pool;

    if(balance_hash != rule->rule.balance_id) return(0);
  }

#ifdef CONFIG_TEXTSEARCH
  if(rule->pattern != NULL) {
    if((hdr->parsed_pkt.payload_offset > 0) && (hdr->caplen > hdr->parsed_pkt.payload_offset)) {
      struct ts_state state;
      char *payload = (char*)&(skb->data[hdr->parsed_pkt.payload_offset-displ]);
      int i, rc, payload_len = hdr->caplen - hdr->parsed_pkt.payload_offset;

      printk("Trying to match pattern [caplen=%d][len=%d][displ=%d][payload_offset=%d][",
	     hdr->caplen, payload_len, displ, hdr->parsed_pkt.payload_offset);

      for(i=0; i<payload_len; i++) printk("%c", payload[i]);
      printk("]\n");

      rc = (textsearch_find_continuous(rule->pattern, &state, payload, payload_len) != UINT_MAX) ? 1 : 0;

      if(rc == 0)
	return(0); /* No match */
    } else
      return(0); /* No payload data */
  }
#endif

  printk("rule->plugin_id [rule_id=%d][filter_plugin_id=%d][plugin_action=%d][ptr=%p]\n", 
	 rule->rule.rule_id, 
	 rule->rule.extended_fields.filter_plugin_id,
	 rule->rule.plugin_action.plugin_id,
	 plugin_registration[rule->rule.plugin_action.plugin_id]);

  if((rule->rule.extended_fields.filter_plugin_id > 0) 
     && (rule->rule.extended_fields.filter_plugin_id < MAX_PLUGIN_ID)
     && (plugin_registration[rule->rule.extended_fields.filter_plugin_id] != NULL)
     && (plugin_registration[rule->rule.extended_fields.filter_plugin_id]->pfring_plugin_filter_skb != NULL)
     ) {
      int rc = plugin_registration[rule->rule.extended_fields.filter_plugin_id]->pfring_plugin_filter_skb(rule, hdr, skb);
      
      if(rc == 0)
	return(0); /* No match */    
  }

  /* Action to be performed in case of match */
  if((rule->rule.plugin_action.plugin_id != 0)
     && (rule->rule.plugin_action.plugin_id < MAX_PLUGIN_ID)      
     && (plugin_registration[rule->rule.plugin_action.plugin_id] != NULL)
     && (plugin_registration[rule->rule.plugin_action.plugin_id]->pfring_plugin_handle_skb != NULL)
     ) {
      plugin_registration[rule->rule.plugin_action.plugin_id]->pfring_plugin_handle_skb(rule, hdr, skb);
  }

  return(1); /* match */
}

/* ********************************** */

static void add_skb_to_ring(struct sk_buff *skb,
			    struct ring_opt *pfr,
			    struct pcap_pkthdr *hdr,
			    int is_ip_pkt,
			    int displ)
{
  FlowSlot *theSlot;
  int idx, fwd_pkt = 0;
  struct list_head *ptr, *tmp_ptr;

  if(!pfr->ring_active) return;

#if defined(RING_DEBUG)
  printk("add_skb_to_ring: [displ=%d][is_ip_pkt=%d][%d -> %d]\n",
	 displ, is_ip_pkt, hdr->parsed_pkt.l4_src_port, hdr->parsed_pkt.l4_dst_port);
#endif

  write_lock_irq(&pfr->ring_index_lock);
  pfr->slots_info->tot_pkts++;

  /* ************************** */

  /* [1] BPF Filtering (from af_packet.c) */
  if(pfr->bpfFilter != NULL) {
    unsigned res = 1, len;

    len = skb->len-skb->data_len;

    skb->data -= displ;
    res = sk_run_filter(skb, pfr->bpfFilter->insns, pfr->bpfFilter->len);
    skb->data += displ;

    if(res == 0) {
      /* Filter failed */
#if defined(RING_DEBUG)
      printk("add_skb_to_ring(skb): Filter failed [len=%d][tot=%llu]"
	     "[insertIdx=%d][pkt_type=%d][cloned=%d]\n",
	     (int)skb->len, pfr->slots_info->tot_pkts,
	     pfr->slots_info->insert_idx,
	     skb->pkt_type, skb->cloned);
#endif

      write_unlock_irq(&pfr->ring_index_lock);
      return;
    }
  }

  /* ************************************* */

#if defined(RING_DEBUG)
  printk("add_skb_to_ring(skb) [len=%d][tot=%llu][insertIdx=%d]"
	 "[pkt_type=%d][cloned=%d]\n",
	 (int)skb->len, pfr->slots_info->tot_pkts,
	 pfr->slots_info->insert_idx,
	 skb->pkt_type, skb->cloned);
#endif

  idx = pfr->slots_info->insert_idx;
  theSlot = get_insert_slot(pfr);

  if((theSlot != NULL) && (theSlot->slot_state == 0)) {
    char *bucket;

    /* Update Index */
    idx++;

    bucket = &theSlot->bucket;
    memcpy(bucket, hdr, sizeof(struct pcap_pkthdr)); /* Copy extended packet header */

    /* Extensions */
    fwd_pkt = pfr->rules_default_accept_policy;

    /* ************************** */
    
    if(0)
    printk("About to evaluate packet [len=%d][tot=%llu][insertIdx=%d]"
	   "[pkt_type=%d][cloned=%d]\n",
	   (int)skb->len, pfr->slots_info->tot_pkts,
	   pfr->slots_info->insert_idx,
	   skb->pkt_type, skb->cloned);

    /* [2] Filter packet according to rules */
    read_lock(&ring_mgmt_lock);
    list_for_each_safe(ptr, tmp_ptr, &pfr->rules)
      {
	filtering_rule_element *entry;

	entry = list_entry(ptr, filtering_rule_element, list);

	if(match_filtering_rule(entry, hdr, skb, displ))
	  {
	    if(entry->rule.rule_action == forward_packet_and_stop_rule_evaluation) {
	      fwd_pkt = 1;
	      break;
	    } else if(entry->rule.rule_action == dont_forward_packet_and_stop_rule_evaluation) {
	      fwd_pkt = 0;
              break;
	    } else if(entry->rule.rule_action == execute_action_and_continue_rule_evaluation) {
	      /* The action has already been performed inside match_filtering_rule()
		 hence instead of stopping rule evaluation, the next rule
		 will be evaluated */
	    }
	  }
      } /* for */
    read_unlock(&ring_mgmt_lock);

    if(fwd_pkt) {
      /* We accept the packet: it needs to be queued */

      /* [3] Packet sampling */
      if(pfr->sample_rate > 1) {
	if(pfr->pktToSample == 0) {
	  pfr->pktToSample = pfr->sample_rate;
	} else {
	  pfr->pktToSample--;

#if defined(RING_DEBUG)
	  printk("add_skb_to_ring(skb): sampled packet [len=%d]"
		 "[tot=%llu][insertIdx=%d][pkt_type=%d][cloned=%d]\n",
		 (int)skb->len, pfr->slots_info->tot_pkts,
		 pfr->slots_info->insert_idx,
		 skb->pkt_type, skb->cloned);
#endif

	  write_unlock_irq(&pfr->ring_index_lock);
	  return;
	}
      }

      /* [4] Check if there is a reflector device defined */
      if((pfr->reflector_dev != NULL)
	 && (!netif_queue_stopped(pfr->reflector_dev)))
	{
	  int cpu = smp_processor_id();

	  /* increase reference counter so that this skb is not freed */
	  atomic_inc(&skb->users);

	  skb->data -= displ;

	  /* send it */
	  if (pfr->reflector_dev->xmit_lock_owner != cpu)
	    {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
	      spin_lock_bh(&pfr->reflector_dev->xmit_lock);
	      pfr->reflector_dev->xmit_lock_owner = cpu;
	      spin_unlock_bh(&pfr->reflector_dev->xmit_lock);
#else
	      netif_tx_lock_bh(pfr->reflector_dev);
#endif
	      if (pfr->reflector_dev->hard_start_xmit(skb, pfr->reflector_dev) == 0) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
		spin_lock_bh(&pfr->reflector_dev->xmit_lock);
		pfr->reflector_dev->xmit_lock_owner = -1;
		spin_unlock_bh(&pfr->reflector_dev->xmit_lock);
#else
		netif_tx_unlock_bh(pfr->reflector_dev);
#endif
		skb->data += displ;
#if defined(RING_DEBUG)
		printk("++ hard_start_xmit succeeded\n");
#endif

		write_unlock_irq(&pfr->ring_index_lock);
		return; /* OK */
	      }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
	      spin_lock_bh(&pfr->reflector_dev->xmit_lock);
	      pfr->reflector_dev->xmit_lock_owner = -1;
	      spin_unlock_bh(&pfr->reflector_dev->xmit_lock);
#else
	      netif_tx_unlock_bh(pfr->reflector_dev);
#endif
	    }

#if defined(RING_DEBUG)
	  printk("++ hard_start_xmit failed\n");
#endif
	  skb->data += displ;
	  write_unlock_irq(&pfr->ring_index_lock);
	  return; /* -ENETDOWN */
	}

      /* No reflector device: the packet needs to be queued */
      if(hdr->caplen > 0) {
	/* Copy the packet into the bucket */
	int copy_len = min(bucket_len, hdr->caplen);
	
	if(copy_len > 0)
	  skb_copy_bits(skb, -displ, &bucket[sizeof(struct pcap_pkthdr)], hdr->caplen);
      }

#if defined(RING_DEBUG)
      {
	static unsigned int lastLoss = 0;

	if(pfr->slots_info->tot_lost
	   && (lastLoss != pfr->slots_info->tot_lost)) {
	  printk("add_skb_to_ring(%d): [data_len=%d]"
		 "[hdr.caplen=%d][skb->len=%d]"
		 "[pcap_pkthdr=%d][removeIdx=%d]"
		 "[loss=%lu][page=%u][slot=%u]\n",
		 idx-1, pfr->slots_info->data_len, hdr->caplen, skb->len,
		 sizeof(struct pcap_pkthdr),
		 pfr->slots_info->remove_idx,
		 (long unsigned int)pfr->slots_info->tot_lost,
		 pfr->insert_page_id, pfr->insert_slot_id);

	  lastLoss = pfr->slots_info->tot_lost;
	}
      }
#endif

      if(idx == pfr->slots_info->tot_slots)
	pfr->slots_info->insert_idx = 0;
      else
	pfr->slots_info->insert_idx = idx;

      pfr->slots_info->tot_insert++;
      theSlot->slot_state = 1;
    }
  } else {
    pfr->slots_info->tot_lost++;

#if defined(RING_DEBUG)
    printk("add_skb_to_ring(skb): packet lost [loss=%lu]"
	   "[removeIdx=%u][insertIdx=%u]\n",
	   (long unsigned int)pfr->slots_info->tot_lost,
	   pfr->slots_info->remove_idx, pfr->slots_info->insert_idx);
#endif
  }

  write_unlock_irq(&pfr->ring_index_lock);

  if(fwd_pkt) {
    /* wakeup in case of poll() */
    if(waitqueue_active(&pfr->ring_slots_waitqueue))
      wake_up_interruptible(&pfr->ring_slots_waitqueue);
  }
}

/* ********************************** */

static u_int hash_skb(ring_cluster_element *cluster_ptr,
		      struct sk_buff *skb,
		      int displ)
{
  u_int idx;
  struct iphdr *ip;

  if(cluster_ptr->cluster.hashing_mode == cluster_round_robin)
    {
      idx = cluster_ptr->cluster.hashing_id++;
    }
  else
    {
      /* Per-flow clustering */
      if(skb->len > sizeof(struct iphdr)+sizeof(struct tcphdr))
	{
	  /*
	    skb->data+displ

	    Always points to to the IP part of the packet
	  */
	  ip = (struct iphdr*)(skb->data+displ);
	  idx = ip->saddr+ip->daddr+ip->protocol;

	  if(ip->protocol == IPPROTO_TCP)
	    {
	      struct tcphdr *tcp = (struct tcphdr*)(skb->data+displ
						    +sizeof(struct iphdr));
	      idx += tcp->source+tcp->dest;
	    }
	  else if(ip->protocol == IPPROTO_UDP)
	    {
	      struct udphdr *udp = (struct udphdr*)(skb->data+displ
						    +sizeof(struct iphdr));
	      idx += udp->source+udp->dest;
	    }
	}
      else
	idx = skb->len;
    }

  return(idx % cluster_ptr->cluster.num_cluster_elements);
}

/* ********************************** */

static int register_plugin(struct pfring_plugin_registration *reg) 
{
  if(reg == NULL) return(-1);

#ifndef RING_DEBUG
  printk("--> register_plugin(%d)\n", reg->plugin_id);
#endif

  if((reg->plugin_id >= MAX_PLUGIN_ID) || (reg->plugin_id == 0))
    return(-EINVAL);

  if(plugin_registration[reg->plugin_id] != NULL) 
    return(-EINVAL); /* plugin already registered */
  else {
    plugin_registration[reg->plugin_id] = reg;
    try_module_get(THIS_MODULE); /* Increment usage count */
    return(0);
  }
}

/* ********************************** */

int unregister_plugin(u_int16_t pfring_plugin_id)
{
  if(pfring_plugin_id >= MAX_PLUGIN_ID)
    return(-EINVAL);

  if(plugin_registration[pfring_plugin_id] == NULL) 
    return(-EINVAL); /* plugin not registered */
  else {
    struct list_head *ptr, *tmp_ptr, *ring_ptr, *ring_tmp_ptr;

    plugin_registration[pfring_plugin_id] = NULL;

    read_lock(&ring_mgmt_lock);
    list_for_each_safe(ring_ptr, ring_tmp_ptr, &ring_table) {
      struct ring_element *entry = list_entry(ring_ptr, struct ring_element, list);
      struct ring_opt *pfr = ring_sk(entry->sk);

      list_for_each_safe(ptr, tmp_ptr, &pfr->rules)
	{
	  filtering_rule_element *rule;
	
	  rule = list_entry(ptr, filtering_rule_element, list);
	
	  if(rule->rule.plugin_action.plugin_id == pfring_plugin_id) {
	    if(rule->plugin_data_ptr != NULL) {
	      kfree(rule->plugin_data_ptr);
	      rule->plugin_data_ptr = NULL;
	    }

	    rule->rule.plugin_action.plugin_id = 0;
	  }
	}
    }
    read_unlock(&ring_mgmt_lock);

    module_put(THIS_MODULE); /* Decrement usage count */
    return(0);
  }
}

/* ********************************** */

static int skb_ring_handler(struct sk_buff *skb,
			    u_char recv_packet,
			    u_char real_skb /* 1=real skb, 0=faked skb */)
{
  struct sock *skElement;
  int rc = 0, is_ip_pkt;
  struct list_head *ptr;
  struct pcap_pkthdr hdr;
  int displ;

#ifdef PROFILING
  uint64_t rdt = _rdtsc(), rdt1, rdt2;
#endif

  if((!skb) /* Invalid skb */
     || ((!enable_tx_capture) && (!recv_packet)))
    {
      /*
	An outgoing packet is about to be sent out
	but we decided not to handle transmitted
	packets.
      */
      return(0);
    }

#if defined(RING_DEBUG)
  if(1) {
    struct timeval tv;

    skb_get_timestamp(skb, &tv);
    printk("skb_ring_handler() [skb=%p][%u.%u][len=%d][dev=%s][csum=%u]\n",
	   skb, (unsigned int)tv.tv_sec, (unsigned int)tv.tv_usec, skb->len,
	   skb->dev->name == NULL ? "<NULL>" : skb->dev->name, skb->csum);
  }
#endif

#ifdef PROFILING
  rdt1 = _rdtsc();
#endif

  if(recv_packet) {
    /* Hack for identifying a packet received by the e1000 */
    if(real_skb)
      displ = SKB_DISPLACEMENT;
    else
      displ = 0; /* Received by the e1000 wrapper */
  } else
    displ = 0;

  is_ip_pkt = parse_pkt(skb, displ, &hdr);

  /* (de)Fragmentation <fusco@ntop.org> */
  if (enable_ip_defrag
      && is_ip_pkt
      && recv_packet
      && (ring_table_size > 0))
    {
      struct sk_buff *cloned = NULL;
      struct iphdr* iphdr = NULL;

      skb_reset_network_header(skb);
      skb_reset_transport_header(skb);
      skb_set_network_header(skb, hdr.parsed_pkt.l3_offset-displ);

      if(((iphdr = ip_hdr(skb)) != NULL)
	 && (iphdr->frag_off & htons(IP_MF | IP_OFFSET)))
	{
	  if((cloned = skb_clone(skb, GFP_ATOMIC)) != NULL)
	    {
	      struct sk_buff *skk = NULL;
#if defined (RING_DEBUG)
	      int offset = ntohs(iphdr->frag_off);
	      offset &= IP_OFFSET;
	      offset <<= 3;

	      printk("There is a fragment to handle [proto=%d][frag_off=%u] [ip_id=%u]\n",
		     iphdr->protocol, offset, ntohs(iphdr->id));
#endif
	      skk = ring_gather_frags(cloned);

	      if(skk != NULL)
		{
#if defined (RING_DEBUG)
		  printk("IP reasm on new skb [skb_len=%d][head_len=%d][nr_frags=%d][frag_list=%p]\n",
			 (int)skk->len, skb_headlen(skk),
			 skb_shinfo(skk)->nr_frags, skb_shinfo(skk)->frag_list);
#endif
		  skb = skk;
		  parse_pkt(skb, displ, &hdr);
		  hdr.len = hdr.caplen = skb->len+displ;
		} else {
		  //printk("Fragment queued \n");
		  return(0); /* mask rcvd fragments */
		}
	    }
	}
      else
	{
#if defined (RING_DEBUG)
	  printk("Do not seems to be a fragmented ip_pkt[iphdr=%p]\n", iphdr);
#endif
	}
    }

  /* BD - API changed for time keeping */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14))
  if(skb->stamp.tv_sec == 0) do_gettimeofday(&skb->stamp);
  hdr.ts.tv_sec = skb->stamp.tv_sec, hdr.ts.tv_usec = skb->stamp.tv_usec;
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22))
  if(skb->tstamp.off_sec == 0) __net_timestamp(skb);
  hdr.ts.tv_sec = skb->tstamp.off_sec, hdr.ts.tv_usec = skb->tstamp.off_usec;
#else /* 2.6.22 and above */
  if(skb->tstamp.tv64 == 0) __net_timestamp(skb);
  hdr.ts = ktime_to_timeval(skb->tstamp);
#endif

  hdr.len = hdr.caplen = skb->len+displ;
  hdr.caplen = min(hdr.caplen, bucket_len);

  read_lock(&ring_mgmt_lock);

  /* [1] Check unclustered sockets */
  list_for_each(ptr, &ring_table) {
    struct ring_opt *pfr;
    struct ring_element *entry;

    entry = list_entry(ptr, struct ring_element, list);

    skElement = entry->sk;
    pfr = ring_sk(skElement);

    if((pfr != NULL)
       && (pfr->cluster_id == 0 /* No cluster */)
       && (pfr->ring_slots != NULL)
       && ((pfr->ring_netdev == skb->dev)
	   || ((skb->dev->flags & IFF_SLAVE)
	       && (pfr->ring_netdev == skb->dev->master)))) {
      /* We've found the ring where the packet can be stored */
      add_skb_to_ring(skb, pfr, &hdr, is_ip_pkt, displ);
      rc = 1; /* Ring found: we've done our job */
    }
  }

  /* [2] Check socket clusters */
  list_for_each(ptr, &ring_cluster_list) {
    ring_cluster_element *cluster_ptr;
    struct ring_opt *pfr;

    cluster_ptr = list_entry(ptr, ring_cluster_element, list);

    if(cluster_ptr->cluster.num_cluster_elements > 0) {
      u_int skb_hash = hash_skb(cluster_ptr, skb, displ);

      skElement = cluster_ptr->cluster.sk[skb_hash];

      if(skElement != NULL) {
	pfr = ring_sk(skElement);

	if((pfr != NULL)
	   && (pfr->ring_slots != NULL)
	   && ((pfr->ring_netdev == skb->dev)
	       || ((skb->dev->flags & IFF_SLAVE)
		   && (pfr->ring_netdev == skb->dev->master)))) {
	  /* We've found the ring where the packet can be stored */
	  add_skb_to_ring(skb, pfr, &hdr, is_ip_pkt, displ);
	  rc = 1; /* Ring found: we've done our job */
	}
      }
    }
  }

  read_unlock(&ring_mgmt_lock);

#ifdef PROFILING
  rdt1 = _rdtsc()-rdt1;
#endif

#ifdef PROFILING
  rdt2 = _rdtsc();
#endif

#if 0
  if(transparent_mode)
#endif
    rc = 0;

  if((rc != 0) && real_skb) {
#if 0
    dev_kfree_skb(skb); /* Free the skb */
#endif
  }

#ifdef PROFILING
  rdt2 = _rdtsc()-rdt2;
  rdt = _rdtsc()-rdt;

#if defined(RING_DEBUG)
  printk("# cycles: %d [lock costed %d %d%%][free costed %d %d%%]\n",
	 (int)rdt, rdt-rdt1,
	 (int)((float)((rdt-rdt1)*100)/(float)rdt),
	 rdt2,
	 (int)((float)(rdt2*100)/(float)rdt));
#endif
#endif

  return(rc); /*  0 = packet not handled */
}

/* ********************************** */

struct sk_buff skb;

static int buffer_ring_handler(struct net_device *dev,
			       char *data, int len) {

#if defined(RING_DEBUG)
  printk("buffer_ring_handler: [dev=%s][len=%d]\n",
	 dev->name == NULL ? "<NULL>" : dev->name, len);
#endif

  skb.dev = dev, skb.len = len, skb.data = data, skb.data_len = len;

  /* BD - API changed for time keeping */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14))
  skb.stamp.tv_sec = 0;
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22))
  skb.tstamp.off_sec = 0;
#else
  skb.tstamp.tv64 = 0;
#endif

  return(skb_ring_handler(&skb, 1, 0 /* fake skb */));
}

/* ********************************** */

static int ring_create(struct socket *sock, int protocol) {
  struct sock *sk;
  struct ring_opt *pfr;
  int err;

#if defined(RING_DEBUG)
  printk("RING: ring_create()\n");
#endif

  /* Are you root, superuser or so ? */
  if(!capable(CAP_NET_ADMIN))
    return -EPERM;

  if(sock->type != SOCK_RAW)
    return -ESOCKTNOSUPPORT;

  if(protocol != htons(ETH_P_ALL))
    return -EPROTONOSUPPORT;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
  MOD_INC_USE_COUNT;
#endif

  err = -ENOMEM;

  // BD: -- broke this out to keep it more simple and clear as to what the
  // options are.
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,11))
  sk = sk_alloc(PF_RING, GFP_KERNEL, 1, NULL);
#else
  // BD: API changed in 2.6.12, ref:
  // http://svn.clkao.org/svnweb/linux/revision/?rev=28201
  sk = sk_alloc(PF_RING, GFP_ATOMIC, &ring_proto, 1);
#endif
#else
  /* Kernel 2.4 */
  sk = sk_alloc(PF_RING, GFP_KERNEL, 1);
#endif

  if (sk == NULL)
    goto out;

  sock->ops = &ring_ops;
  sock_init_data(sock, sk);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,11))
  sk_set_owner(sk, THIS_MODULE);
#endif
#endif

  err = -ENOMEM;
  ring_sk(sk) = ring_sk_datatype(kmalloc(sizeof(*pfr), GFP_KERNEL));

  if (!(pfr = ring_sk(sk))) {
    sk_free(sk);
    goto out;
  }
  memset(pfr, 0, sizeof(*pfr));
  pfr->ring_active = 0; /* We activate as soon as somebody waits for packets */
  init_waitqueue_head(&pfr->ring_slots_waitqueue);
  rwlock_init(&pfr->ring_index_lock);
  atomic_set(&pfr->num_ring_slots_waiters, 0);
  INIT_LIST_HEAD(&pfr->rules);
  
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
  sk->sk_family       = PF_RING;
  sk->sk_destruct     = ring_sock_destruct;
#else
  sk->family          = PF_RING;
  sk->destruct        = ring_sock_destruct;
  sk->num             = protocol;
#endif

  ring_insert(sk);

#if defined(RING_DEBUG)
  printk("RING: ring_create() - created\n");
#endif

  return(0);
 out:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
  MOD_DEC_USE_COUNT;
#endif
  return err;
}

/* *********************************************** */

static int ring_release(struct socket *sock)
{
  struct sock *sk = sock->sk;
  struct ring_opt *pfr = ring_sk(sk);
  struct list_head *ptr, *tmp_ptr;
  void * ring_memory_ptr;

  if(!sk)  return 0;

#if defined(RING_DEBUG)
  printk("RING: called ring_release\n");
#endif

  /*
    The calls below must be placed outside the
    write_lock_irq...write_unlock_irq block.
  */
  sock_orphan(sk);
  ring_proc_remove(ring_sk(sk));

  write_lock_irq(&ring_mgmt_lock);
  ring_remove(sk);
  sock->sk = NULL;

  list_for_each_safe(ptr, tmp_ptr, &pfr->rules)
    {
      filtering_rule_element *rule;

      rule = list_entry(ptr, filtering_rule_element, list);

#if defined(RING_DEBUG)
      printk("RING: Deleting rule_id %d\n", rule->rule.core_fields.rule_id);
#endif

#ifdef CONFIG_TEXTSEARCH
      if(rule->pattern) textsearch_destroy(rule->pattern);
#endif
      list_del(ptr);
      kfree(rule);
    }

  /* Free the ring buffer later, vfree needs interrupts enabled */
  ring_memory_ptr = pfr->ring_memory;

  ring_sk(sk) = NULL;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
  skb_queue_purge(&sk->sk_write_queue);
#endif

  sock_put(sk);
  write_unlock_irq(&ring_mgmt_lock);

  if(ring_memory_ptr != NULL) {
#if defined(RING_DEBUG)
    printk("RING: ring_release: rvfree\n");
#endif
    rvfree(pfr->ring_memory, pfr->slots_info->tot_mem);
  }

  kfree(pfr);

#if defined(RING_DEBUG)
  printk("RING: ring_release: rvfree done\n");
#endif

#if defined(RING_DEBUG)
  printk("RING: ring_release: done\n");
#endif

  return 0;
}

/* ********************************** */
/*
 * We create a ring for this socket and bind it to the specified device
 */
static int packet_ring_bind(struct sock *sk, struct net_device *dev)
{
  u_int the_slot_len;
  u_int32_t tot_mem;
  struct ring_opt *pfr = ring_sk(sk);
  // struct page *page, *page_end;

  if(!dev) return(-1);

#if defined(RING_DEBUG)
  printk("RING: packet_ring_bind(%s) called\n", dev->name);
#endif

  /* **********************************************

  *************************************
  *                                   *
  *        FlowSlotInfo               *
  *                                   *
  ************************************* <-+
  *        FlowSlot                   *   |
  *************************************   |
  *        FlowSlot                   *   |
  *************************************   +- num_slots
  *        FlowSlot                   *   |
  *************************************   |
  *        FlowSlot                   *   |
  ************************************* <-+

  ********************************************** */

  the_slot_len = sizeof(u_char)    /* flowSlot.slot_state */
#ifdef RING_MAGIC
    + sizeof(u_char)
#endif
    + sizeof(struct pcap_pkthdr)
    + bucket_len      /* flowSlot.bucket */;

  tot_mem = sizeof(FlowSlotInfo) + num_slots*the_slot_len;
  if (tot_mem % PAGE_SIZE)
    tot_mem += PAGE_SIZE - (tot_mem % PAGE_SIZE);

  pfr->ring_memory = rvmalloc(tot_mem);

  if (pfr->ring_memory != NULL) {
    printk("RING: successfully allocated %lu bytes at 0x%08lx\n", (unsigned long) tot_mem, (unsigned long) pfr->ring_memory);
  } else {
    printk("RING: ERROR: not enough memory for ring\n");
    return(-1);
  }

  // memset(pfr->ring_memory, 0, tot_mem); // rvmalloc does the memset already

  pfr->slots_info = (FlowSlotInfo*)pfr->ring_memory;
  pfr->ring_slots = (char*)(pfr->ring_memory+sizeof(FlowSlotInfo));

  pfr->slots_info->version     = RING_FLOWSLOT_VERSION;
  pfr->slots_info->slot_len    = the_slot_len;
  pfr->slots_info->data_len    = bucket_len;
  pfr->slots_info->tot_slots   = (tot_mem-sizeof(FlowSlotInfo))/the_slot_len;
  pfr->slots_info->tot_mem     = tot_mem;
  pfr->slots_info->sample_rate = 1;

  printk("RING: allocated %d slots [slot_len=%d][tot_mem=%u]\n",
	 pfr->slots_info->tot_slots, pfr->slots_info->slot_len,
	 pfr->slots_info->tot_mem);

#ifdef RING_MAGIC
  {
    int i;

    for(i=0; i<pfr->slots_info->tot_slots; i++) {
      unsigned long idx = i*pfr->slots_info->slot_len;
      FlowSlot *slot = (FlowSlot*)&pfr->ring_slots[idx];
      slot->magic = RING_MAGIC_VALUE; slot->slot_state = 0;
    }
  }
#endif

  pfr->sample_rate = 1; /* No sampling */
  pfr->insert_page_id = 1, pfr->insert_slot_id = 0;
  pfr->rules_default_accept_policy = 1, pfr->num_filtering_rules = 0;

  /*
    IMPORTANT
    Leave this statement here as last one. In fact when
    the ring_netdev != NULL the socket is ready to be used.
  */
  pfr->ring_netdev = dev;

  return(0);
}

/* ************************************* */

/* Bind to a device */
static int ring_bind(struct socket *sock,
		     struct sockaddr *sa, int addr_len)
{
  struct sock *sk=sock->sk;
  struct net_device *dev = NULL;

#if defined(RING_DEBUG)
  printk("RING: ring_bind() called\n");
#endif

  /*
   *	Check legality
   */
  if(addr_len != sizeof(struct sockaddr))
    return -EINVAL;
  if(sa->sa_family != PF_RING)
    return -EINVAL;
  if(sa->sa_data == NULL)
    return -EINVAL;

  /* Safety check: add trailing zero if missing */
  sa->sa_data[sizeof(sa->sa_data)-1] = '\0';

#if defined(RING_DEBUG)
  printk("RING: searching device %s\n", sa->sa_data);
#endif

  if((dev = __dev_get_by_name(sa->sa_data)) == NULL) {
#if defined(RING_DEBUG)
    printk("RING: search failed\n");
#endif
    return(-EINVAL);
  } else
    return(packet_ring_bind(sk, dev));
}

/* ************************************* */

/*
 * rvmalloc / rvfree / kvirt_to_pa copied from usbvideo.c
 */
unsigned long kvirt_to_pa(unsigned long adr)
{
  unsigned long kva, ret;
  
  kva = (unsigned long) page_address(vmalloc_to_page((void *)adr));
  kva |= adr & (PAGE_SIZE-1); /* restore the offset */
  ret = __pa(kva);
  return ret;
}

/* ************************************* */


static int ring_mmap(struct file *file,
		     struct socket *sock,
		     struct vm_area_struct *vma)
{
  struct sock *sk = sock->sk;
  struct ring_opt *pfr = ring_sk(sk);

  unsigned long size, start;
  unsigned long page;
  char *ptr;

  size = (unsigned long)(vma->vm_end - vma->vm_start);

#if defined(RING_DEBUG)
  printk("RING: ring_mmap() called, size: %ld bytes\n", size);
#endif

  if(pfr->ring_memory == NULL) {
#if defined(RING_DEBUG)
    printk("RING: ring_mmap() failed: mapping area to an unbound socket\n");
#endif
    return -EINVAL;
  }

  if(size % PAGE_SIZE) {
#if defined(RING_DEBUG)
    printk("RING: ring_mmap() failed: len is not multiple of PAGE_SIZE\n");
#endif
    return(-EINVAL);
  }

  /* if userspace tries to mmap beyond end of our buffer, fail */
  if(size > pfr->slots_info->tot_mem) {
#if defined(RING_DEBUG)
    printk("RING: ring_mmap() failed: area too large [%ld > %d]\n", size, pfr->slots_info->tot_mem);
#endif
    return(-EINVAL);
  }

#if defined(RING_DEBUG)
  printk("RING: mmap [slot_len=%d][tot_slots=%d] for ring on device %s\n",
	 pfr->slots_info->slot_len, pfr->slots_info->tot_slots,
	 pfr->ring_netdev->name);
#endif

  /* we do not want to have this area swapped out, lock it */
  vma->vm_flags |= VM_LOCKED;
  start = vma->vm_start;

  /* Ring slots start from page 1 (page 0 is reserved for FlowSlotInfo) */
  ptr = (char *)(pfr->ring_memory);

  while(size > 0)
    {
      int rc;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11))
      page = vmalloc_to_pfn(ptr);
      rc =remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED);
#else
      page = vmalloc_to_page(ptr);
      page = kvirt_to_pa(ptr);
      rc = remap_page_range(vma, start, page, PAGE_SIZE, PAGE_SHARED);
#endif
      if(rc) {
#if defined(RING_DEBUG)
	printk("RING: remap_pfn_range() failed\n");
#endif
	return(-EAGAIN);
      }
      start += PAGE_SIZE;
      ptr   += PAGE_SIZE;
      if (size > PAGE_SIZE) {
	size -= PAGE_SIZE;
      } else {
	size = 0;
      }
    }

#if defined(RING_DEBUG)
  printk("RING: ring_mmap succeeded\n");
#endif

  return 0;
}

/* ************************************* */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
static int ring_recvmsg(struct kiocb *iocb, struct socket *sock,
			struct msghdr *msg, size_t len, int flags)
#else
     static int ring_recvmsg(struct socket *sock, struct msghdr *msg, int len,
			     int flags, struct scm_cookie *scm)
#endif
{
  FlowSlot* slot;
  struct ring_opt *pfr = ring_sk(sock->sk);
  u_int32_t queued_pkts, num_loops = 0;

#if defined(RING_DEBUG)
  printk("ring_recvmsg called\n");
#endif

  pfr->ring_active = 1;
  slot = get_remove_slot(pfr);

  while((queued_pkts = num_queued_pkts(pfr)) < MIN_QUEUED_PKTS) {
    wait_event_interruptible(pfr->ring_slots_waitqueue, 1);

#if defined(RING_DEBUG)
    printk("-> ring_recvmsg returning %d [queued_pkts=%d][num_loops=%d]\n",
	   slot->slot_state, queued_pkts, num_loops);
#endif

    if(queued_pkts > 0) {
      if(num_loops++ > MAX_QUEUE_LOOPS)
	break;
    }
  }

#if defined(RING_DEBUG)
  if(slot != NULL)
    printk("ring_recvmsg is returning [queued_pkts=%d][num_loops=%d]\n",
	   queued_pkts, num_loops);
#endif

  return(queued_pkts);
}

/* ************************************* */

unsigned int ring_poll(struct file * file,
		       struct socket *sock, poll_table *wait)
{
  FlowSlot* slot;
  struct ring_opt *pfr = ring_sk(sock->sk);

#if defined(RING_DEBUG)
  printk("poll called\n");
#endif

  pfr->ring_active = 1;
  slot = get_remove_slot(pfr);

  if((slot != NULL) && (slot->slot_state == 0))
    poll_wait(file, &pfr->ring_slots_waitqueue, wait);

#if defined(RING_DEBUG)
  printk("poll returning %d\n", slot->slot_state);
#endif

  if((slot != NULL) && (slot->slot_state == 1))
    return(POLLIN | POLLRDNORM);
  else
    return(0);
}

/* ************************************* */

int add_to_cluster_list(ring_cluster_element *el,
			struct sock *sock) {

  if(el->cluster.num_cluster_elements == CLUSTER_LEN)
    return(-1); /* Cluster full */

  ring_sk_datatype(ring_sk(sock))->cluster_id = el->cluster.cluster_id;
  el->cluster.sk[el->cluster.num_cluster_elements] = sock;
  el->cluster.num_cluster_elements++;
  return(0);
}

/* ************************************* */

int remove_from_cluster_list(struct ring_cluster *el,
			     struct sock *sock) {
  int i, j;

  for(i=0; i<CLUSTER_LEN; i++)
    if(el->sk[i] == sock) {
      el->num_cluster_elements--;

      if(el->num_cluster_elements > 0) {
	/* The cluster contains other elements */
	for(j=i; j<CLUSTER_LEN-1; j++)
	  el->sk[j] = el->sk[j+1];

	el->sk[CLUSTER_LEN-1] = NULL;
      } else {
	/* Empty cluster */
	memset(el->sk, 0, sizeof(el->sk));
      }

      return(0);
    }

  return(-1); /* Not found */
}

/* ************************************* */

static int remove_from_cluster(struct sock *sock,
			       struct ring_opt *pfr)
{
  struct list_head *ptr, *tmp_ptr;

#if defined(RING_DEBUG)
  printk("--> remove_from_cluster(%d)\n", pfr->cluster_id);
#endif

  if(pfr->cluster_id == 0 /* 0 = No Cluster */)
    return(0); /* Noting to do */

  list_for_each_safe(ptr, tmp_ptr, &ring_cluster_list) {
    ring_cluster_element *cluster_ptr;

    cluster_ptr = list_entry(ptr, ring_cluster_element, list);

    if(cluster_ptr->cluster.cluster_id == pfr->cluster_id) {
      return(remove_from_cluster_list(&cluster_ptr->cluster, sock));
    }
  }

  return(-EINVAL); /* Not found */
}

/* ************************************* */

static int add_to_cluster(struct sock *sock,
			  struct ring_opt *pfr,
			  u_short cluster_id)
{
  struct list_head *ptr, *tmp_ptr;
  ring_cluster_element *cluster_ptr;

#ifndef RING_DEBUG
  printk("--> add_to_cluster(%d)\n", cluster_id);
#endif

  if(cluster_id == 0 /* 0 = No Cluster */) return(-EINVAL);

  if(pfr->cluster_id != 0)
    remove_from_cluster(sock, pfr);

  list_for_each_safe(ptr, tmp_ptr, &ring_cluster_list) {
    cluster_ptr = list_entry(ptr, ring_cluster_element, list);

    if(cluster_ptr->cluster.cluster_id == cluster_id) {
      return(add_to_cluster_list(cluster_ptr, sock));
    }
  }


  /* There's no existing cluster. We need to create one */
  if((cluster_ptr = kmalloc(sizeof(ring_cluster_element), GFP_KERNEL)) == NULL)
    return(-ENOMEM);

  INIT_LIST_HEAD(&cluster_ptr->list);

  cluster_ptr->cluster.cluster_id = cluster_id;
  cluster_ptr->cluster.num_cluster_elements = 1;
  cluster_ptr->cluster.hashing_mode = cluster_per_flow; /* Default */
  cluster_ptr->cluster.hashing_id   = 0;

  memset(cluster_ptr->cluster.sk, 0, sizeof(cluster_ptr->cluster.sk));
  cluster_ptr->cluster.sk[0] = sock;
  pfr->cluster_id = cluster_id;

  list_add(&cluster_ptr->list, &ring_cluster_list); /* Add as first entry */

  return(0); /* 0 = OK */
}

/* ************************************* */

/* Code taken/inspired from core/sock.c */
static int ring_setsockopt(struct socket *sock,
			   int level, int optname,
			   char __user *optval, int optlen)
{
  struct ring_opt *pfr = ring_sk(sock->sk);
  int val, found, ret = 0 /* OK */;
  u_int cluster_id, debug = 0;
  char devName[8];
  struct list_head *prev = NULL;
  filtering_rule_element *entry, *rule;
  u_int16_t rule_id;

  if(pfr == NULL)
    return(-EINVAL);

  if (get_user(val, (int *)optval))
    return -EFAULT;

  found = 1;

  switch(optname)
    {
    case SO_ATTACH_FILTER:
      ret = -EINVAL;
      if (optlen == sizeof(struct sock_fprog))
	{
	  unsigned int fsize;
	  struct sock_fprog fprog;
	  struct sk_filter *filter;

	  ret = -EFAULT;

	  /*
	    NOTE

	    Do not call copy_from_user within a held
	    splinlock (e.g. ring_mgmt_lock) as this caused
	    problems when certain debugging was enabled under
	    2.6.5 -- including hard lockups of the machine.
	  */
	  if(copy_from_user(&fprog, optval, sizeof(fprog)))
	    break;

	  /* Fix below courtesy of Noam Dev <noamdev@gmail.com> */
	  fsize  = sizeof(struct sock_filter) * fprog.len;
	  filter = kmalloc(fsize + sizeof(struct sk_filter), GFP_KERNEL);

	  if(filter == NULL)
	    {
	      ret = -ENOMEM;
	      break;
	    }

	  if(copy_from_user(filter->insns, fprog.filter, fsize))
	    break;

	  filter->len = fprog.len;

	  if(sk_chk_filter(filter->insns, filter->len) != 0)
	    {
	      /* Bad filter specified */
	      kfree(filter);
	      pfr->bpfFilter = NULL;
	      break;
	    }

	  /* get the lock, set the filter, release the lock */
	  write_lock(&ring_mgmt_lock);
	  pfr->bpfFilter = filter;
	  write_unlock(&ring_mgmt_lock);
	  ret = 0;
	}
      break;

    case SO_DETACH_FILTER:
      write_lock(&ring_mgmt_lock);
      found = 1;
      if(pfr->bpfFilter != NULL)
	{
	  kfree(pfr->bpfFilter);
	  pfr->bpfFilter = NULL;
	  write_unlock(&ring_mgmt_lock);
	  break;
	}
      ret = -ENONET;
      break;

    case SO_ADD_TO_CLUSTER:
      if (optlen!=sizeof(val))
	return -EINVAL;

      if (copy_from_user(&cluster_id, optval, sizeof(cluster_id)))
	return -EFAULT;

      write_lock(&ring_mgmt_lock);
      ret = add_to_cluster(sock->sk, pfr, cluster_id);
      write_unlock(&ring_mgmt_lock);
      break;

    case SO_REMOVE_FROM_CLUSTER:
      write_lock(&ring_mgmt_lock);
      ret = remove_from_cluster(sock->sk, pfr);
      write_unlock(&ring_mgmt_lock);
      break;

    case SO_SET_REFLECTOR:
      if(optlen >= (sizeof(devName)-1))
	return -EINVAL;

      if(optlen > 0)
	{
	  if(copy_from_user(devName, optval, optlen))
	    return -EFAULT;
	}

      devName[optlen] = '\0';

#if defined(RING_DEBUG)
      printk("+++ SO_SET_REFLECTOR(%s)\n", devName);
#endif

      write_lock(&ring_mgmt_lock);
      pfr->reflector_dev = dev_get_by_name(devName);
      write_unlock(&ring_mgmt_lock);

#if defined(RING_DEBUG)
      if(pfr->reflector_dev != NULL)
	printk("SO_SET_REFLECTOR(%s): succeded\n", devName);
      else
	printk("SO_SET_REFLECTOR(%s): device unknown\n", devName);
#endif
      break;

    case SO_TOGGLE_FILTER_POLICY:
      if(optlen != sizeof(u_int8_t))
	return -EINVAL;
      else {
	u_int8_t new_policy;

	if(copy_from_user(&new_policy, optval, optlen))
	  return -EFAULT;

	write_lock(&ring_mgmt_lock);
	pfr->rules_default_accept_policy = new_policy;
	write_unlock(&ring_mgmt_lock);
	if(debug) printk("SO_TOGGLE_FILTER_POLICY: default policy is %s\n",
			 pfr->rules_default_accept_policy ? "accept" : "drop");
      }
      break;

    case SO_ADD_FILTERING_RULE:
      if(debug) printk("+++ SO_ADD_FILTERING_RULE(len=%d)\n", optlen);

      if(optlen != sizeof(filtering_rule)) 
	{
	  if(debug) printk("bad len (expected %d)\n", sizeof(filtering_rule));
	  return -EINVAL;
	}
      else
	{
	  struct list_head *ptr, *tmp_ptr;

	  if(debug) printk("Allocating memory\n");

	  rule = (filtering_rule_element*)kmalloc(sizeof(filtering_rule_element), GFP_KERNEL);

	  if(rule == NULL)
	    return -EFAULT;
	  else
	    memset(rule, 0, sizeof(filtering_rule_element));

	  if(copy_from_user(&rule->rule, optval, optlen))
	    return -EFAULT;

	  INIT_LIST_HEAD(&rule->list);

#ifdef CONFIG_TEXTSEARCH
	  /* Compile pattern if present */
	  if(strlen(rule->rule.extended_fields.payload_pattern) > 0)
	    {
	      rule->pattern = textsearch_prepare("kmp", rule->rule.extended_fields.payload_pattern,
						 strlen(rule->rule.extended_fields.payload_pattern),
						 GFP_KERNEL, TS_AUTOLOAD);

	      if(IS_ERR(rule->pattern)) {
		printk("RING: Unable to compile pattern '%s'\n",
		       rule->rule.extended_fields.payload_pattern);
		rule->pattern = NULL;
	      }
	    } else
	      rule->pattern = NULL;
#endif
	  write_lock(&ring_mgmt_lock);

	  if(debug) printk("SO_ADD_FILTERING_RULE: About to add rule %d\n", rule->rule.rule_id);

	  /* Implement an ordered add */
	  list_for_each_safe(ptr, tmp_ptr, &pfr->rules)
	    {
	      entry = list_entry(ptr, filtering_rule_element, list);

	      if(debug) printk("SO_ADD_FILTERING_RULE: [current rule %d][rule to add %d]\n",
			       entry->rule.rule_id, rule->rule.rule_id);

	      if(entry->rule.rule_id == rule->rule.rule_id)
		{
		  memcpy(&entry->rule, &rule->rule, sizeof(filtering_rule));
#ifdef CONFIG_TEXTSEARCH
		  if(entry->pattern != NULL) textsearch_destroy(entry->pattern);
		  entry->pattern = rule->pattern;
#endif
		  kfree(rule);
		  rule = NULL;
		  if(debug) printk("SO_ADD_FILTERING_RULE: overwritten rule_id %d\n", entry->rule.rule_id);
		  break;
		} else if(entry->rule.rule_id > rule->rule.rule_id)
		  {
		    if(prev == NULL)
		      {
			list_add(&rule->list, &pfr->rules); /* Add as first entry */
			pfr->num_filtering_rules++;
			if(debug) printk("SO_ADD_FILTERING_RULE: added rule %d as head rule\n", rule->rule.rule_id);
		      }
		    else {
		      list_add(&rule->list, prev);
		      pfr->num_filtering_rules++;
		      if(debug) printk("SO_ADD_FILTERING_RULE: added rule %d\n", rule->rule.rule_id);
		    }

		    rule = NULL;
		    break;
		  } else
		    prev = ptr;
	    } /* for */

	  if(rule != NULL)
	    {
	      if(prev == NULL)
		{
		  list_add(&rule->list, &pfr->rules); /* Add as first entry */
		  pfr->num_filtering_rules++;
		  if(debug) printk("SO_ADD_FILTERING_RULE: added rule %d as first rule\n", rule->rule.rule_id);
		}
	      else
		{
		  list_add_tail(&rule->list, &pfr->rules); /* Add as first entry */
		  pfr->num_filtering_rules++;
		  if(debug) printk("SO_ADD_FILTERING_RULE: added rule %d as last rule\n", rule->rule.rule_id);
		}
	    }
	  write_unlock(&ring_mgmt_lock);
	}
      break;

    case SO_REMOVE_FILTERING_RULE:
      if(optlen != sizeof(u_int16_t /* rule _id */))
	return -EINVAL;
      else
	{
	  u_int8_t rule_found = 0;
	  struct list_head *ptr, *tmp_ptr;

	  if(copy_from_user(&rule_id, optval, optlen))
	    return -EFAULT;

	  write_lock(&ring_mgmt_lock);

	  list_for_each_safe(ptr, tmp_ptr, &pfr->rules)
	    {
	      entry = list_entry(ptr, filtering_rule_element, list);

	      if(entry->rule.rule_id == rule_id)
		{
#ifdef CONFIG_TEXTSEARCH
		  if(entry->pattern) textsearch_destroy(entry->pattern);
#endif
		  list_del(ptr);
		  pfr->num_filtering_rules--;
		  if(entry->plugin_data_ptr != NULL) kfree(entry->plugin_data_ptr);
		  kfree(ptr);
		  if(debug) printk("SO_REMOVE_FILTERING_RULE: rule %d has been removed\n", rule_id);
		  rule_found = 1;
		  break;
		}
	    } /* for */

	  write_unlock(&ring_mgmt_lock);
	  if(!rule_found) {
	    if(debug) printk("SO_REMOVE_FILTERING_RULE: rule %d does not exist\n", rule_id);
	    return -EFAULT; /* Rule not found */
	  }
	}
      break;

    case SO_SET_SAMPLING_RATE:
      if(optlen != sizeof(pfr->sample_rate))
	return -EINVAL;

      if(copy_from_user(&pfr->sample_rate, optval, sizeof(pfr->sample_rate)))
	return -EFAULT;
      break;

    case SO_SET_FILTERING_RULE_PLUGIN_ID:
      {
	struct rule_plugin_id info;

	if(optlen != sizeof(info))
	  return -EINVAL;
	
	if(copy_from_user(&info, optval, sizeof(info)))
	  return -EFAULT;

	if(info.plugin_id >= MAX_PLUGIN_ID) 
	  {
	    printk("plugin_id=%d is out of range\n", info.plugin_id);
	    return -EINVAL;
	  }

	/* Unknown plugin */
	if(plugin_registration[info.plugin_id] == NULL) {
	  printk("plugin_id=%d is not registered\n", info.plugin_id);
	  return -EINVAL;
	}
      }
      break;
      
    default:
      found = 0;
      break;
    }

  if(found)
    return(ret);
  else
    return(sock_setsockopt(sock, level, optname, optval, optlen));
}

/* ************************************* */

static int ring_getsockopt(struct socket *sock,
			   int level, int optname,
			   char __user *optval,
			   int __user *optlen)
{
  int len;
  struct ring_opt *pfr = ring_sk(sock->sk);

  if(pfr == NULL)
    return(-EINVAL);

  if(get_user(len, optlen))
    return -EFAULT;

  if(len < 0)
    return -EINVAL;

  switch(optname)
    {
    case SO_GET_RING_VERSION:
      {
	u_int32_t version = RING_VERSION_NUM;

	if(copy_to_user(optval, &version, sizeof(version)))
	  return -EFAULT;
      }
      break;

    case PACKET_STATISTICS:
      {
	struct tpacket_stats st;

	if (len > sizeof(struct tpacket_stats))
	  len = sizeof(struct tpacket_stats);

	st.tp_packets = pfr->slots_info->tot_insert;
	st.tp_drops   = pfr->slots_info->tot_lost;

	if (copy_to_user(optval, &st, len))
	  return -EFAULT;
	break;
      }

    case SO_GET_FILTERING_RULE_STATS:
      {
	u_int16_t rule_id;
	char *buffer = NULL;
	int rc = -EFAULT;
	struct list_head *ptr, *tmp_ptr;

	if(len < sizeof(rule_id))
	  return -EINVAL;
	
	if(copy_from_user(&rule_id, optval, sizeof(rule_id)))
	  return -EFAULT;

	printk("SO_GET_FILTERING_RULE_STATS: rule_id=%d\n", rule_id);

	write_lock(&ring_mgmt_lock);
	list_for_each_safe(ptr, tmp_ptr, &pfr->rules)
	  {
	    filtering_rule_element *rule;
	    
	    rule = list_entry(ptr, filtering_rule_element, list);
	    if(rule->rule.rule_id == rule_id) 
	      {
		buffer = kmalloc(len, GFP_ATOMIC);

		if(buffer == NULL)
		  rc = -EFAULT;
		else {
		  if(plugin_registration[rule->rule.plugin_action.plugin_id]->pfring_plugin_get_stats == NULL)
		    rc = -EFAULT;
		  else
		    rc = plugin_registration[rule->rule.plugin_action.plugin_id]->pfring_plugin_get_stats(rule, buffer, len);

		  if(rc > 0) {
		    if(copy_to_user(optval, buffer, rc))
		      rc = -EFAULT;
		  }
		}
		break;
	      }
	  }
	write_unlock(&ring_mgmt_lock);

	if(buffer != NULL) kfree(buffer);

	return(rc);
	break;
      }

    default:
      return -ENOPROTOOPT;
    }

  if(put_user(len, optlen))
    return -EFAULT;
  else
    return(0);
}

/* ************************************* */

static int ring_ioctl(struct socket *sock,
		      unsigned int cmd, unsigned long arg)
{
  switch(cmd)
    {
#ifdef CONFIG_INET
    case SIOCGIFFLAGS:
    case SIOCSIFFLAGS:
    case SIOCGIFCONF:
    case SIOCGIFMETRIC:
    case SIOCSIFMETRIC:
    case SIOCGIFMEM:
    case SIOCSIFMEM:
    case SIOCGIFMTU:
    case SIOCSIFMTU:
    case SIOCSIFLINK:
    case SIOCGIFHWADDR:
    case SIOCSIFHWADDR:
    case SIOCSIFMAP:
    case SIOCGIFMAP:
    case SIOCSIFSLAVE:
    case SIOCGIFSLAVE:
    case SIOCGIFINDEX:
    case SIOCGIFNAME:
    case SIOCGIFCOUNT:
    case SIOCSIFHWBROADCAST:
      return(inet_dgram_ops.ioctl(sock, cmd, arg));
#endif

    default:
      return -ENOIOCTLCMD;
    }

  return 0;
}

/* ************************************* */

static struct proto_ops ring_ops = {
  .family	=	PF_RING,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
  .owner	=	THIS_MODULE,
#endif

  /* Operations that make no sense on ring sockets. */
  .connect	=	sock_no_connect,
  .socketpair	=	sock_no_socketpair,
  .accept	=	sock_no_accept,
  .getname	=	sock_no_getname,
  .listen	=	sock_no_listen,
  .shutdown	=	sock_no_shutdown,
  .sendpage	=	sock_no_sendpage,
  .sendmsg	=	sock_no_sendmsg,

  /* Now the operations that really occur. */
  .release	=	ring_release,
  .bind		=	ring_bind,
  .mmap		=	ring_mmap,
  .poll		=	ring_poll,
  .setsockopt	=	ring_setsockopt,
  .getsockopt	=	ring_getsockopt,
  .ioctl	=	ring_ioctl,
  .recvmsg	=	ring_recvmsg,
};

/* ************************************ */

static struct net_proto_family ring_family_ops = {
  .family	=	PF_RING,
  .create	=	ring_create,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
  .owner	=	THIS_MODULE,
#endif
};

// BD: API changed in 2.6.12, ref:
// http://svn.clkao.org/svnweb/linux/revision/?rev=28201
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,11))
static struct proto ring_proto = {
  .name		=	"PF_RING",
  .owner	=	THIS_MODULE,
  .obj_size	=	sizeof(struct sock),
};
#endif

/* ************************************ */

static void __exit ring_exit(void)
{
  struct list_head *ptr, *tmp_ptr;
  struct ring_element *entry;

  list_for_each_safe(ptr, tmp_ptr, &ring_table) {
    entry = list_entry(ptr, struct ring_element, list);
    list_del(ptr);
    kfree(entry);
  }

  list_for_each_safe(ptr, tmp_ptr, &ring_cluster_list) {
    ring_cluster_element *cluster_ptr;

    cluster_ptr = list_entry(ptr, ring_cluster_element, list);

    list_del(ptr);
    kfree(cluster_ptr);
  }

  set_register_pfring_plugin(NULL);
  set_unregister_pfring_plugin(NULL);
  set_skb_ring_handler(NULL);
  set_buffer_ring_handler(NULL);
  sock_unregister(PF_RING);
  ring_proc_term();
  printk("PF_RING: unloaded\n");
}

/* ************************************ */

static int __init ring_init(void)
{
  printk("Welcome to PF_RING %s\n"
	 "(C) 2004-07 L.Deri <deri@ntop.org>\n",
	 RING_VERSION);

  INIT_LIST_HEAD(&ring_table);
  INIT_LIST_HEAD(&ring_cluster_list);

  sock_register(&ring_family_ops);

  set_skb_ring_handler(skb_ring_handler);
  set_buffer_ring_handler(buffer_ring_handler);
  set_register_pfring_plugin(register_plugin);
  set_unregister_pfring_plugin(unregister_plugin);

  if(get_buffer_ring_handler() != buffer_ring_handler) {
    printk("PF_RING: set_buffer_ring_handler FAILED\n");

    set_skb_ring_handler(NULL);
    set_buffer_ring_handler(NULL);
    sock_unregister(PF_RING);
    return -1;
  } else {
    printk("PF_RING: Bucket length    %d bytes\n", bucket_len);
    printk("PF_RING: Ring slots       %d\n", num_slots);
    printk("PF_RING: Slot version     %d\n", RING_FLOWSLOT_VERSION);
    printk("PF_RING: Capture TX       %s\n",
	   enable_tx_capture ? "Yes [RX+TX]" : "No [RX only]");
    printk("PF_RING: IP Defragment    %s\n",  enable_ip_defrag ? "Yes" : "No");

    printk("PF_RING: initialized correctly\n");

    ring_proc_init();
    return 0;
  }
}

module_init(ring_init);
module_exit(ring_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luca Deri <deri@ntop.org>");
MODULE_DESCRIPTION("Packet capture acceleration by means of a ring buffer");

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
MODULE_ALIAS_NETPROTO(PF_RING);
#endif
