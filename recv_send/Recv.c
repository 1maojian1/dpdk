
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include <stdio.h>
#include <arpa/inet.h>


#define NUM_MBUFS (4096-1)

#define BURST_SIZE 32

//从哪个口出数据
int gDpdkPortId = 0;

static const struct rte_eth_conf port_conf_default = {
	.rxmode = {.max_rx_pkt_len = RTE_ETHER_MAX_LEN }
};

static void ng_init_port(struct rte_mempool *mbuf_pool) {

	uint16_t nb_sys_ports= rte_eth_dev_count_avail(); //检测端口是否可用
	if (nb_sys_ports == 0) {
		rte_exit(EXIT_FAILURE, "No Supported eth found\n");
	}
	//设置网卡信息
	struct rte_eth_dev_info dev_info;
	rte_eth_dev_info_get(gDpdkPortId, &dev_info); //得到初始网卡信息，与dpdk还无关
	
	const int num_rx_queues = 1;
	const int num_tx_queues = 0;
	struct rte_eth_conf port_conf = port_conf_default;
	rte_eth_dev_configure(gDpdkPortId, num_rx_queues, num_tx_queues, &port_conf);


	if (rte_eth_rx_queue_setup(gDpdkPortId, 0 , 128, 
		rte_eth_dev_socket_id(gDpdkPortId),NULL, mbuf_pool) < 0) {

		rte_exit(EXIT_FAILURE, "Could not setup RX queue\n");

	}

	if (rte_eth_dev_start(gDpdkPortId) < 0 ) {
		rte_exit(EXIT_FAILURE, "Could not start\n");
	}

	

}


//创建一个数据包
static void create_eth_ip_udp(uint8_t *msg,size_t total_len, uint8_t *dst_mac, uint32_t src_ip, uint32_t dst_ip, uint16 src_port, uint16 dst_port){
	//msg加上mac，IP，udp头部
	struct rte_ether_addr src_mac;
	
	struct rte_ether_hdr *eth = (struct rte_ether_hdr *)msg;
	rte_memcpy(eth->d_addr.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);

	rte_eth_macaddr_get(gDpdkPortId, &src_mac);
	rte_memcpy(eth->s_addr, src_mac, RTE_ETHER_ADDR_LEN);
	eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);

	struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr*)(eth + 1);
	ip->version_ihl = 0x45;
	ip->type_of_service = 0;
	ip->total_length = htons(total_len - sizeof(struct rte_ether_hdr));


	
}


int main(int argc, char *argv[]){
	//环境初始化
	if(rte_eal_init(argc, argv) < 0){
		rte_exit(EXIT_FAILURE, "Error with EAL init\n");
	}

	//确定内存池
	struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("mbuf pool", NUM_MBUFS, 0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if(mbuf_pool == NULL){
		rte_exit(EXIT_FAILURE, "Could nit create mbuf_pool\n");
	}

	ng_init_port(mbuf_pool);

	while(1){
		struct rte_mbuf *mbufs[BURST_SIZE];
		unsigned num_recvd = rte_eth_rx_burst(gDpdkPortId, 0, mbufs, BURST_SIZE);	//从端口中接受放到mbufs中

		if(num_recvd > BURST_SIZE){
			rte_exit(EXIT_FAILURE, "ERROR receiving from eth\n");	//溢出
		}

		unsigned i = 0;
		for(i = 0;i < num_recvd;i++){
			struct rte_ether_hdr *ethr = rte_pktmbuf_mtod(mbufs[i], struct rte_ether_hdr*);//取出以太网数据
			//判断是否ipv4
			if(ethr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)){
				continue;
			}

			struct rte_ipv4_hdr *iphdr =  rte_pktmbuf_mtod_offset(mbufs[i], struct rte_ipv4_hdr *, 
				sizeof(struct rte_ether_hdr));
			//处理UDP
			if(iphdr->next_proto_id == IPPROTO_UDP){
				//取出包头
				struct rte_udp_hdr *udpdhr = (struct rte_udp_hdr*)(iphdr + 1);

				uint16_t length = ntohs(udpdhr->dgram_len);
				*((char *)udpdhr + length) = '\0';

				//ip + 端口
				struct in_addr addr;
				addr.s_addr = iphdr->src_addr;
				printf("src: %s:%d, ", inet_ntoa(addr), udphdr->src_port);

				addr.s_addr = iphdr->dst_addr;
				printf("dst: %s:%d, %s\n", inet_ntoa(addr), udphdr->src_port, 
					(char *)(udphdr+1));

				//释放到内存池中
				rte_pktmbuf_free(mbufs[i]);
			}
			
		}
	}
}






