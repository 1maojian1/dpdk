

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_timer.h>

#include <stdio.h>
#include <arpa/inet.h>
#include "arp.h"


#define ENABLE_SEND		1
#define ENABLE_ARP		1
#define ENABLE_ICMP     1
#define ENABLE_ARP_TABLE	1

#define ENABLE_DEBUG	1
#define ENABLE_TIMER	1
#define ENABLE_ARP_REPLY	1

#define TIMER_RESOLUTION_CYCLES 120000000000ULL /* 10ms * 1000 * 6 */

#define NUM_MBUFS (4096-1)

#define BURST_SIZE	32

#if ENABLE_SEND


#define MAKE_IPV4_ADDR(a,b,c,d) (a + (b << 8) + (c << 16) + (d << 24))

static uint32_t gLocalIp = MAKE_IPV4_ADDR(192,168,5,133);


static uint32_t gSrcIp; //
static uint32_t gDstIp;

static uint8_t gSrcMac[RTE_ETHER_ADDR_LEN];
static uint8_t gDstMac[RTE_ETHER_ADDR_LEN];

static uint16_t gSrcPort;
static uint16_t gDstPort;

#endif


#if ENABLE_ARP_REPLY

static uint8_t gDefaultArpMac[RTE_ETHER_ADDR_LEN] = {0xFF,0xFF,0xFF,0xFF};

#endif


int gDpdkPortId = 0;



static const struct rte_eth_conf port_conf_default = {
	.rxmode = {.max_rx_pkt_len = RTE_ETHER_MAX_LEN }
};

static void ng_init_port(struct rte_mempool *mbuf_pool) {

	uint16_t nb_sys_ports= rte_eth_dev_count_avail(); //
	if (nb_sys_ports == 0) {
		rte_exit(EXIT_FAILURE, "No Supported eth found\n");
	}

	struct rte_eth_dev_info dev_info;
	rte_eth_dev_info_get(gDpdkPortId, &dev_info); //
	
	const int num_rx_queues = 1;
	const int num_tx_queues = 1;
	struct rte_eth_conf port_conf = port_conf_default;
	rte_eth_dev_configure(gDpdkPortId, num_rx_queues, num_tx_queues, &port_conf);


	if (rte_eth_rx_queue_setup(gDpdkPortId, 0 , 1024, 
		rte_eth_dev_socket_id(gDpdkPortId),NULL, mbuf_pool) < 0) {

		rte_exit(EXIT_FAILURE, "Could not setup RX queue\n");

	}
	
#if ENABLE_SEND
	struct rte_eth_txconf txq_conf = dev_info.default_txconf;
	txq_conf.offloads = port_conf.rxmode.offloads;
	if (rte_eth_tx_queue_setup(gDpdkPortId, 0 , 1024, 
		rte_eth_dev_socket_id(gDpdkPortId), &txq_conf) < 0) {
		
		rte_exit(EXIT_FAILURE, "Could not setup TX queue\n");
		
	}
#endif

	if (rte_eth_dev_start(gDpdkPortId) < 0 ) {
		rte_exit(EXIT_FAILURE, "Could not start\n");
	}

	

}


static int ng_encode_udp_pkt(uint8_t *msg, unsigned char *data, uint16_t total_len) {

	// encode 

	// 1 ethhdr 加上mac
	struct rte_ether_hdr *eth = (struct rte_ether_hdr *)msg;
	rte_memcpy(eth->s_addr.addr_bytes, gSrcMac, RTE_ETHER_ADDR_LEN);
	rte_memcpy(eth->d_addr.addr_bytes, gDstMac, RTE_ETHER_ADDR_LEN);
	eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);
	

	// 2 iphdr 
	struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(msg + sizeof(struct rte_ether_hdr));
	ip->version_ihl = 0x45;
	ip->type_of_service = 0;
	ip->total_length = htons(total_len - sizeof(struct rte_ether_hdr));
	ip->packet_id = 0;
	ip->fragment_offset = 0;
	ip->time_to_live = 64; // ttl = 64
	ip->next_proto_id = IPPROTO_UDP;
	ip->src_addr = gSrcIp;
	ip->dst_addr = gDstIp;
	
	ip->hdr_checksum = 0;
	ip->hdr_checksum = rte_ipv4_cksum(ip);

	// 3 udphdr 

	struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(msg + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
	udp->src_port = gSrcPort;
	udp->dst_port = gDstPort;
	uint16_t udplen = total_len - sizeof(struct rte_ether_hdr) - sizeof(struct rte_ipv4_hdr);
	udp->dgram_len = htons(udplen);

	rte_memcpy((uint8_t*)(udp+1), data, udplen);

	udp->dgram_cksum = 0;
	udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip, udp);

	struct in_addr addr;
	addr.s_addr = gSrcIp;
	printf(" --> src: %s:%d, ", inet_ntoa(addr), ntohs(gSrcPort));

	addr.s_addr = gDstIp;
	printf("dst: %s:%d\n", inet_ntoa(addr), ntohs(gDstPort));

	return 0;
}


static struct rte_mbuf * ng_send(struct rte_mempool *mbuf_pool, uint8_t *data, uint16_t length) {

	// mempool --> mbuf   从mempool中拿出需要使用的内存位置
	//mac头+ip头+udp头 = 42
	const unsigned total_len = length + 42;
	//从内存池中分配
	struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);
	if (!mbuf) {
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc\n");
	}
	mbuf->pkt_len = total_len;
	mbuf->data_len = total_len;

	uint8_t *pktdata = rte_pktmbuf_mtod(mbuf, uint8_t*);

	ng_encode_udp_pkt(pktdata, data, total_len);

	return mbuf;

}


#if ENABLE_ARP
//组织arp包    链路层协议
//注意：当第一次发送arp request时，以太网头和arp头中的目的mac不同，以太网00：00：00：00，arp协议FF:FF:FF:FF
static int  ng_encode_arp_pkt(uint8_t *msg, uint16_t opcode, uint8_t *dst_mac, uint32_t sip, uint32_t dip){
	//以太网头 + arp头
	// 1 ethhdr 加上mac
	struct rte_ether_hdr *eth = (struct rte_ether_hdr *)msg;
	rte_memcpy(eth->s_addr.addr_bytes, gSrcMac, RTE_ETHER_ADDR_LEN);
	if(!strncmp((const char*)dst_mac,(const char*)gDefaultArpMac,RTE_ETHER_ADDR_LEN)){
		uint8_t mac[RTE_ETHER_ADDR_LEN] = {0x0};
		rte_memcpy(eth->d_addr.addr_bytes, mac, RTE_ETHER_ADDR_LEN);
	}
	else 
		rte_memcpy(eth->d_addr.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
	eth->ether_type = htons(RTE_ETHER_TYPE_ARP);

	//2 arp
	struct rte_arp_hdr *arp = (struct rte_arp_hdr *)(eth + 1);
	arp->arp_hardware = htons(1);
	arp->arp_protocol = htons(RTE_ETHER_TYPE_IPV4);
	arp->arp_hlen = RTE_ETHER_ADDR_LEN;
	arp->arp_plen = sizeof(uint32_t);
	arp->arp_opcode = htons(opcode);

	rte_memcpy(arp->arp_data.arp_sha.addr_bytes, gSrcMac, RTE_ETHER_ADDR_LEN);
	rte_memcpy(arp->arp_data.arp_tha.addr_bytes, dst_mac,RTE_ETHER_ADDR_LEN);

	arp->arp_data.arp_sip = sip;
	arp->arp_data.arp_tip = dip;
	
}

static struct rte_mbuf *ng_send_arp(struct rte_mempool *mbuf_pool, uint16_t opcode, uint8_t *dst_mac, uint32_t sip, uint32_t dip){

	 const unsigned total_length = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);
	 struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);
	 if (!mbuf) {
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc\n");
	}
	 mbuf->pkt_len = total_length;
	 mbuf->data_len = total_length;

	 uint8_t *pktdata = rte_pktmbuf_mtod(mbuf, uint8_t*);
	 ng_encode_arp_pkt(pktdata,opcode, dst_mac, sip, dip);

	 return mbuf;
}


#endif

#if ENABLE_ICMP

static uint16_t ng_checksum(uint16_t *addr, int count){
	register long sum = 0;

	while(count > 1){
		sum += *(unsigned short*)addr++;
		count -= 2;
	}

	if(count > 0){
		sum += *(unsigned char *)addr;
	}

	while(sum >> 16){
		sum = (sum & 0xffff) + (sum >> 6);
	}

	return ~sum;
}

//
static int ng_encode_icmp_pkt(uint8_t *msg, uint8_t *dst_mac, uint32_t sip, uint32_t dip, uint16_t id, uint16_t seqnb){
	//1 eth
	struct rte_ether_hdr *eth = (struct rte_ether_hdr *)msg;
	rte_memcpy(eth->s_addr.addr_bytes, gSrcMac, RTE_ETHER_ADDR_LEN);
	rte_memcpy(eth->d_addr.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
	eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);
	
	//2 ip
	struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(msg + sizeof(struct rte_ether_hdr));
	ip->version_ihl = 0x45;
	ip->type_of_service = 0;
	ip->total_length = htons(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_icmp_hdr));
	ip->packet_id = 0;
	ip->fragment_offset = 0;
	ip->time_to_live = 64; // ttl = 64
	ip->next_proto_id = IPPROTO_ICMP;
	ip->src_addr = sip;
	ip->dst_addr = dip;
	
	ip->hdr_checksum = 0;
	ip->hdr_checksum = rte_ipv4_cksum(ip);
	
	//3 icmp 
	struct rte_icmp_hdr *icmp = (struct rte_icmp_hdr *)(msg + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
	icmp->icmp_code = 0;
	icmp->icmp_type = RTE_IP_ICMP_ECHO_REPLY;
	icmp->icmp_ident = id;
	icmp->icmp_seq_nb = seqnb;
	
	icmp->icmp_cksum = 0;
	icmp->icmp_cksum = ng_checksum((uint16_t *)icmp, sizeof(struct rte_icmp_hdr));

	return 0;
}


static struct rte_mbuf *ng_send_icmp(struct rte_mempool *mbuf_pool, 
			uint8_t *dst_mac, uint32_t sip, uint32_t dip, uint16_t id, uint16_t seqnb){

	const unsigned total_length = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr)  + sizeof(struct rte_icmp_hdr);
	struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);
 
	if(!mbuf){
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc\n");
	}

	mbuf->pkt_len = total_length;
	mbuf->data_len = total_length;

	uint8_t *pktdata = rte_pktmbuf_mtod(mbuf, uint8_t *);
	ng_encode_icmp_pkt(pktdata, dst_mac, sip, dip, id, seqnb);

	return mbuf;
}


#endif 

static inline void print_ether_addr(const char *what, const struct rte_ether_addr *eth_addr){
	char buf[RTE_ETHER_ADDR_FMT_SIZE];
	rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, eth_addr);
	printf("%s%s", what, buf);
}

#if ENABLE_TIMER
//定期发送arp包
static void arp_request_timer_cb(__attribute__((unused)) struct rte_timer *tim,
	    void *arg){

	//定时组织发送的包

	struct rte_mempool *mbuf_pool = (struct rte_mempool*)arg;
#if 0
	struct rte_mbuf *arpbuf = ng_send_arp(mbuf_pool, ahdr->arp_data.arp_sha.addr_bytes,
			ahdr->arp_data.arp_tip, ahdr->arp_data.arp_sip);

	rte_eth_tx_burst(gDpdkPortId, 0, &arpbuf, 1);
	rte_pktmbuf_free(arpbuf);
#endif
	//广播发送
	int i = 0;
	for(i = 1;i <= 254;i++){
		uint32_t dstip = (gLocalIp & 0x00FFFFFF) | (0xFF000000 & (i << 24));
		struct rte_mbuf *arpbuf = NULL;
		uint8_t *dstmac = ng_get_dst_macaddr(dstip);
		if(dstmac == NULL){
			//目前没有，arphdr->mac : FF:FF:FF:FF
			//ether->mac 00:00:00:00
		 	
			arpbuf = ng_send_arp(mbuf_pool, RTE_ARP_OP_REQUEST, gDefaultArpMac, gLocalIp, dstip);
		}else{
			arpbuf = ng_send_arp(mbuf_pool, RTE_ARP_OP_REQUEST, dstmac, gLocalIp,dstip);
		}

		rte_eth_tx_burst(gDpdkPortId, 0, &arpbuf, 1);
		rte_pktmbuf_free(arpbuf);

	}
	
}

#endif


int main(int argc, char *argv[]) {

	if (rte_eal_init(argc, argv) < 0) {
		rte_exit(EXIT_FAILURE, "Error with EAL init\n");
		
	}

	struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("mbuf pool", NUM_MBUFS,
		0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (mbuf_pool == NULL) {
		rte_exit(EXIT_FAILURE, "Could not create mbuf pool\n");
	}

	ng_init_port(mbuf_pool);

	rte_eth_macaddr_get(gDpdkPortId, (struct rte_ether_addr *)gSrcMac);

#if ENABLE_TIMER
	//启动定时器
	
	/* init RTE timer library */
	rte_timer_subsystem_init();

	/* init timer structures */
	struct rte_timer arp_timer;
	rte_timer_init(&arp_timer);

	/* load timer0, every second, on master lcore, reloaded automatically */
	uint64_t hz = rte_get_timer_hz();
	unsigned lcore_id = rte_lcore_id();
	rte_timer_reset(&arp_timer, hz, PERIODICAL, lcore_id, arp_request_timer_cb, mbuf_pool);	//重复触发

#endif

	while (1) {

		struct rte_mbuf *mbufs[BURST_SIZE];
		unsigned num_recvd = rte_eth_rx_burst(gDpdkPortId, 0, mbufs, BURST_SIZE);		//从网卡中接收数据
		if (num_recvd > BURST_SIZE) {
			rte_exit(EXIT_FAILURE, "Error receiving from eth\n");
		}

		unsigned i = 0;
		for (i = 0;i < num_recvd;i ++) {
			//取出eth头
			struct rte_ether_hdr *ehdr = rte_pktmbuf_mtod(mbufs[i], struct rte_ether_hdr*);

#if ENABLE_ARP
			if(ehdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP)){

				struct rte_arp_hdr *ahdr = rte_pktmbuf_mtod_offset(mbufs[i], struct rte_arp_hdr *, 
					sizeof(struct rte_ether_hdr));		//读取地址为mbufs[i] + ether头，强转为arp_hdr

				struct in_addr addr;
				addr.s_addr = ahdr->arp_data.arp_tip;
				printf("arp ---> src: %s ", inet_ntoa(addr));
				
				addr.s_addr = gLocalIp;
				printf(" local: %s \n", inet_ntoa(addr));
				
				//对于arp报文，1 arp request 2 arp reply	
				//arp采用广播 识别是否是自己的包
				if(ahdr->arp_data.arp_tip == gLocalIp){
					//对于收到的arp询问包
					if(ahdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)){
						struct rte_mbuf *arpbuf = ng_send_arp(mbuf_pool, RTE_ARP_OP_REPLY, ahdr->arp_data.arp_sha.addr_bytes,
							ahdr->arp_data.arp_tip, ahdr->arp_data.arp_sip);

						rte_eth_tx_burst(gDpdkPortId, 0, &arpbuf, 1);
						rte_pktmbuf_free(arpbuf);

						rte_pktmbuf_free(mbufs[i]);
					}else if(ahdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY)){
						//对于收到的arp回复包
						struct arp_table *table = arp_table_instance();

						uint8_t *hwaddr= ng_get_dst_macaddr(ahdr->arp_data.arp_sip);
						//原来arp表中没有该ip
						if(hwaddr == NULL){
							struct arp_entry *entry = rte_malloc("arp entry", sizeof(struct arp_entry), 0);
							if(entry){
								memset(entry,0,sizeof(struct arp_entry));
								entry->ip = ahdr->arp_data.arp_sip;
								
								rte_memcpy(entry->hwaddr, ahdr->arp_data.arp_sha.addr_bytes, RTE_ETHER_ADDR_LEN);
								entry->type = ARP_ENTRY_STATUS_DYNAMIC;

								LL_ADD(entry, table->entries);
								table->count++;
							}
						}
#if ENABLE_DEBUG
						struct arp_entry *iter;
						for(iter = table->entries; iter != NULL; iter = iter->next){
							print_ether_addr("arp entry --> mac", (struct rte_ether_addr *)iter->hwaddr);
							struct in_addr addr;
							addr.s_addr = iter->ip;
							printf("arp ---> src: %s ", inet_ntoa(addr));
						}
#endif
					
					}
				}

				continue;
			}

#endif 
		
			if (ehdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
				continue;
			}

			struct rte_ipv4_hdr *iphdr =  rte_pktmbuf_mtod_offset(mbufs[i], struct rte_ipv4_hdr *, 
				sizeof(struct rte_ether_hdr));
			
			if (iphdr->next_proto_id == IPPROTO_UDP) {

				struct rte_udp_hdr *udphdr = (struct rte_udp_hdr *)(iphdr + 1);

#if ENABLE_SEND //

				rte_memcpy(gDstMac, ehdr->s_addr.addr_bytes, RTE_ETHER_ADDR_LEN);
				
				rte_memcpy(&gSrcIp, &iphdr->dst_addr, sizeof(uint32_t));
				rte_memcpy(&gDstIp, &iphdr->src_addr, sizeof(uint32_t));

				rte_memcpy(&gSrcPort, &udphdr->dst_port, sizeof(uint16_t));
				rte_memcpy(&gDstPort, &udphdr->src_port, sizeof(uint16_t));

#endif

				uint16_t length = ntohs(udphdr->dgram_len);
				*((char*)udphdr + length) = '\0';

				struct in_addr addr;
				addr.s_addr = iphdr->src_addr;
				printf("src: %s:%d, ", inet_ntoa(addr), ntohs(udphdr->src_port));

				addr.s_addr = iphdr->dst_addr;
				printf("dst: %s:%d, %s\n", inet_ntoa(addr), ntohs(udphdr->dst_port), 
					(char *)(udphdr+1));

#if ENABLE_SEND

				struct rte_mbuf *txbuf = ng_send(mbuf_pool, (uint8_t *)(udphdr+1), length);
				rte_eth_tx_burst(gDpdkPortId, 0, &txbuf, 1);
				rte_pktmbuf_free(txbuf);
				
#endif

				rte_pktmbuf_free(mbufs[i]);
			}

#if ENABLE_ICMP

			if(iphdr->next_proto_id == IPPROTO_ICMP){

				struct rte_icmp_hdr *icmphdr = (struct rte_icmp_hdr *)(iphdr + 1);
				
				struct in_addr addr;
				addr.s_addr = iphdr->src_addr;
				printf("icmp ---> src: %s ", inet_ntoa(addr));

				if(icmphdr->icmp_type == RTE_IP_ICMP_ECHO_REQUEST){

					addr.s_addr = iphdr->dst_addr;
					printf(" local: %s , type : %d\n", inet_ntoa(addr), icmphdr->icmp_type);

				
					struct rte_mbuf *txbuf = ng_send_icmp(mbuf_pool, ehdr->s_addr.addr_bytes, iphdr->dst_addr, iphdr->src_addr,
						icmphdr->icmp_ident, icmphdr->icmp_seq_nb);

					rte_eth_tx_burst(gDpdkPortId, 0, &txbuf, 1);
					rte_pktmbuf_free(txbuf);

					rte_pktmbuf_free(mbufs[i]);
				}

			
			}
	
#endif
			
		}

#if ENABLE_TIMER
		//当前值 - 上一次触发的时间 > TIMER_RESOLUTION_CYCLES就发送
		static uint64_t prev_tsc = 0, cur_tsc;
		uint64_t diff_tsc;

		cur_tsc = rte_rdtsc();
		diff_tsc = cur_tsc - prev_tsc;
		if (diff_tsc > TIMER_RESOLUTION_CYCLES) {
			rte_timer_manage();
			prev_tsc = cur_tsc;
		}

#endif

	}

}




        
